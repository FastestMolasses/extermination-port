/* em_platform_ios.m — iOS platform layer on UIKit (docs/IOS.md).
 *
 * Clean-room, no third-party libraries; only Apple system frameworks
 * (UIKit, QuartzCore, GameController). Manual reference counting, like the
 * macOS layer. As on macOS, the platform layer never touches a GPU API: it
 * makes the CAMetalLayer and sizes its drawable; the Metal backend
 * configures the layer's device and format.
 *
 * THREADS. UIApplicationMain owns the main thread and its run loop, so the
 * engine runs on its own "game thread": main.c's bring-up (compiled for iOS
 * as em_port_main, Makefile "iOS") and the frame loop em_frame_run, whose
 * own pacing keeps the game's 59.94 Hz tick exactly as on macOS — the
 * display's refresh rate never drives the logic. The hand-off to UIKit:
 *   - the view's CAMetalLayer is the window's native handle; its drawable
 *     size is set on the main thread, and the Metal backend sets its device
 *     and format on the main thread too (em_gfx_create), then takes
 *     drawables from it and presents them on the game thread;
 *   - the drawable size is published by the main thread's layout through
 *     atomics (em_window_drawable_size);
 *   - em_window_poll, called at the top of every frame (step C), parks the
 *     game thread while the app is not active: an app in the background may
 *     not submit GPU work, and the PS2 game had no background either. The
 *     scene's didEnterBackground waits until the thread is parked.
 *
 * FILES. The game opens assets/... and writes data/save relative to the
 * working directory. The bundle is read-only, so the working directory is
 * the app's Documents directory: "assets" there is a symlink to the bundle's
 * assets/ (re-made at every launch, since an install moves the bundle), and
 * data/ is a real directory seeded from the bundle's data/. Everything the
 * game writes (saves, test/trace files) lands in Documents. When nothing is
 * attached to stdout (a launch from the home screen), stdout and stderr go
 * to Documents/extermination.log.
 *
 * INPUT. A game controller only (GameController.framework; the same backend
 * as macOS, em_gamepad_mac.m). There are no touch controls: with no
 * extended gamepad connected the screen shows "Connect a controller".
 */
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <GameController/GameController.h>

#include "em_platform.h"
#include "audio/ios/em_audio_ios.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* main.c's main(), renamed for this target by the Makefile. */
extern int em_port_main(void);

struct EmWindow {
    CAMetalLayer *layer;
};

/* ---- shared state between the main thread and the game thread -------- */
static CAMetalLayer   *s_layer;          /* set once, before the game starts */
static atomic_int      s_drawable_w, s_drawable_h;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_cond = PTHREAD_COND_INITIALIZER;
static int             s_active;         /* the scene is foreground-active */
static int             s_parked;         /* the game thread waits in poll */
static int             s_running;        /* the game thread exists */
static int             s_log_redirected; /* stdout/stderr go to the log */

/* Any EM_*TEST switch: an automated run, which ends the process with the
 * game's exit status (the macOS binary's behaviour), so a console attached
 * by `devicectl device process launch --console` sees it. */
static int automated_run(void)
{
    for (char **e = environ; e && *e; ++e) {
        const char *v = *e;
        if (strncmp(v, "EM_", 3) != 0) continue;
        const char *eq = strchr(v, '=');
        size_t n = eq ? (size_t)(eq - v) : strlen(v);
        if (n >= 7 && !strncmp(v + n - 4, "TEST", 4)) return 1;
    }
    return 0;
}

/* ---- the view: backed by a CAMetalLayer -------------------------------- */
@interface EmMetalView : UIView
@end

@implementation EmMetalView
+ (Class)layerClass { return [CAMetalLayer class]; }

- (instancetype)initWithFrame:(CGRect)frame
{
    if ((self = [super initWithFrame:frame]))
        self.backgroundColor = [UIColor blackColor];
    return self;
}

/* The drawable covers the whole screen in native pixels; the renderer
 * places the 4:3 game frame in its centre (black bars at the sides). */
- (void)publishDrawableSize
{
    CGSize size = self.bounds.size;
    CGFloat scale = self.contentScaleFactor;
    int w = (int)lround(size.width * scale), h = (int)lround(size.height * scale);
    if (w <= 0 || h <= 0) return;
    ((CAMetalLayer *)self.layer).drawableSize = CGSizeMake(w, h);
    atomic_store(&s_drawable_w, w);
    atomic_store(&s_drawable_h, h);
}

- (void)didMoveToWindow
{
    [super didMoveToWindow];
    UIScreen *screen = self.window.windowScene.screen;
    if (screen) self.contentScaleFactor = screen.nativeScale;
    [self publishDrawableSize];
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    [self publishDrawableSize];
}
@end

/* ---- the view controller: landscape, no status bar, the controller note */
@interface EmViewController : UIViewController
@end

@implementation EmViewController {
    UILabel *_note;   /* owned by the view hierarchy */
}

- (void)loadView
{
    EmMetalView *view = [[EmMetalView alloc] initWithFrame:CGRectZero];
    self.view = view;
    [view release];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    UILabel *note = [[UILabel alloc] initWithFrame:CGRectZero];
    note.text = @"Connect a controller";
    note.textColor = [UIColor whiteColor];
    note.font = [UIFont boldSystemFontOfSize:24.0];
    note.textAlignment = NSTextAlignmentCenter;
    note.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.75];
    note.layer.cornerRadius = 12.0;
    note.layer.masksToBounds = YES;
    note.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:note];
    [NSLayoutConstraint activateConstraints:@[
        [note.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [note.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
        [note.widthAnchor constraintEqualToConstant:360.0],
        [note.heightAnchor constraintEqualToConstant:72.0],
    ]];
    _note = note;
    [note release];

    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:self selector:@selector(controllerDidConnect:)
               name:GCControllerDidConnectNotification object:nil];
    [nc addObserver:self selector:@selector(controllerDidDisconnect:)
               name:GCControllerDidDisconnectNotification object:nil];
    [self updateNote];
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [super dealloc];
}

static void log_controller(const char *what, GCController *c)
{
    fprintf(stderr, "controller: %s \"%s\" (%s)\n", what,
            c.vendorName ? c.vendorName.UTF8String : "unnamed",
            c.extendedGamepad ? "extended gamepad" : "no extended gamepad profile: not used");
}

- (void)controllerDidConnect:(NSNotification *)note
{
    log_controller("connected", note.object);
    [self updateNote];
}

- (void)controllerDidDisconnect:(NSNotification *)note
{
    log_controller("disconnected", note.object);
    [self updateNote];
}

/* The game reads GCController.current's extended gamepad (em_gamepad_mac.m);
 * the note shows while no connected controller has that profile. */
- (void)updateNote
{
    BOOL have = NO;
    for (GCController *c in [GCController controllers])
        if (c.extendedGamepad) { have = YES; break; }
    _note.hidden = have;
}

- (void)showStopped:(int)status
{
    _note.text = [NSString stringWithFormat:@"The game stopped (status %d)", status];
    _note.hidden = NO;
    [NSLayoutConstraint deactivateConstraints:_note.constraints];
    [_note.widthAnchor constraintEqualToConstant:520.0].active = YES;
    [_note.heightAnchor constraintEqualToConstant:72.0].active = YES;
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    return UIInterfaceOrientationMaskLandscape;
}
@end

/* ---- the game thread ----------------------------------------------------- */
static EmViewController *s_controller;   /* main thread only */

static void *game_thread_main(void *arg)
{
    (void)arg;
    pthread_setname_np("em.game");
    const int status = em_port_main();
    fflush(stdout);
    fflush(stderr);
    pthread_mutex_lock(&s_lock);
    s_running = 0;
    pthread_cond_broadcast(&s_cond);
    pthread_mutex_unlock(&s_lock);
    if (automated_run())
        _exit(status);
    /* A normal launch only returns on a fail-stop (the reason is on
     * stderr, i.e. in Documents/extermination.log): say so on screen. */
    dispatch_async(dispatch_get_main_queue(), ^{
        [s_controller showStopped:status];
    });
    return NULL;
}

static void start_game_thread(CAMetalLayer *layer)
{
    if (s_layer) return;   /* one game per process */
    s_layer = [layer retain];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    /* The macOS build runs the engine on the main thread (8 MB of stack);
     * a secondary thread's default is far smaller. */
    pthread_attr_setstacksize(&attr, (size_t)32 << 20);
    pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INTERACTIVE, 0);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    s_running = 1;
    if (pthread_create(&thread, &attr, game_thread_main, NULL) != 0) {
        s_running = 0;
        fprintf(stderr, "fatal: could not start the game thread\n");
    }
    pthread_attr_destroy(&attr);
}

static void set_active(int active)
{
    pthread_mutex_lock(&s_lock);
    s_active = active;
    pthread_cond_broadcast(&s_cond);
    pthread_mutex_unlock(&s_lock);
}

/* Wait (bounded) until the game thread is parked in em_window_poll. */
static void wait_parked(double seconds)
{
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += (time_t)seconds;
    until.tv_nsec += (long)((seconds - (double)(time_t)seconds) * 1e9);
    if (until.tv_nsec >= 1000000000L) { until.tv_nsec -= 1000000000L; until.tv_sec++; }
    pthread_mutex_lock(&s_lock);
    while (s_running && !s_parked && !s_active)
        if (pthread_cond_timedwait(&s_cond, &s_lock, &until) == ETIMEDOUT) break;
    pthread_mutex_unlock(&s_lock);
}

/* ---- launch: working directory and log ----------------------------------- */
static void redirect_output(const char *docs)
{
    /* Keep a console that is attached (a pty from devicectl/Xcode, a pipe). */
    struct stat st;
    if (isatty(STDOUT_FILENO) ||
        (fstat(STDOUT_FILENO, &st) == 0 && (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode))))
        return;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/extermination.log", docs);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
    s_log_redirected = 1;
}

static void launch_setup(void)
{
    @autoreleasepool {
        NSFileManager *fm = [NSFileManager defaultManager];
        NSString *docs = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                                              NSUserDomainMask, YES) firstObject];
        NSString *res = [[NSBundle mainBundle] resourcePath];
        if (!docs || !res) {
            fprintf(stderr, "fatal: no Documents directory or bundle resources\n");
            return;
        }
        [fm createDirectoryAtPath:docs withIntermediateDirectories:YES attributes:nil error:nil];
        redirect_output(docs.fileSystemRepresentation);

        /* assets -> the bundle's read-only assets (the bundle moves with
         * every install, so the link is made again at each launch). */
        NSString *link = [docs stringByAppendingPathComponent:@"assets"];
        NSString *target = [res stringByAppendingPathComponent:@"assets"];
        struct stat st;
        if (lstat(link.fileSystemRepresentation, &st) == 0 && S_ISLNK(st.st_mode))
            unlink(link.fileSystemRepresentation);
        if (symlink(target.fileSystemRepresentation, link.fileSystemRepresentation) != 0)
            fprintf(stderr, "ios: cannot link %s -> %s: %s\n", link.fileSystemRepresentation,
                    target.fileSystemRepresentation, strerror(errno));
        if (![fm fileExistsAtPath:target])
            fprintf(stderr, "ios: the bundle has no assets/ (build with tools/ios/build.sh, "
                            "which copies the local exports into the app)\n");

        /* data/: writable, seeded from the bundle's data/ on first launch. */
        NSString *data = [docs stringByAppendingPathComponent:@"data"];
        NSString *seed = [res stringByAppendingPathComponent:@"data"];
        if (![fm fileExistsAtPath:data] && [fm fileExistsAtPath:seed])
            [fm copyItemAtPath:seed toPath:data error:nil];

        if (chdir(docs.fileSystemRepresentation) != 0)
            fprintf(stderr, "ios: cannot enter %s: %s\n", docs.fileSystemRepresentation,
                    strerror(errno));
        printf("ios: working directory %s (assets from %s)%s\n", docs.fileSystemRepresentation,
               res.fileSystemRepresentation, s_log_redirected ? "; this log is extermination.log" : "");
        fflush(stdout);
    }
    /* Category Playback before anything plays (AVPlayer movies included). */
    (void)em_audio_ios_session_begin();
}

/* ---- scene and application delegates ------------------------------------- */
@interface EmSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (nonatomic, retain) UIWindow *window;
@end

@implementation EmSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session
      options:(UISceneConnectionOptions *)options
{
    (void)session;
    (void)options;
    if (![scene isKindOfClass:[UIWindowScene class]]) return;
    UIWindow *window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    EmViewController *controller = [[EmViewController alloc] init];
    window.rootViewController = controller;
    self.window = window;
    [window makeKeyAndVisible];
    [controller.view layoutIfNeeded];   /* the drawable size before the game starts */
    s_controller = controller;           /* kept alive by the window */
    [controller release];
    [window release];
    start_game_thread((CAMetalLayer *)s_controller.view.layer);
}

- (void)sceneDidBecomeActive:(UIScene *)scene
{
    (void)scene;
    [UIApplication sharedApplication].idleTimerDisabled = YES;
    em_audio_ios_set_suspended(0);
    set_active(1);
}

- (void)sceneWillResignActive:(UIScene *)scene
{
    (void)scene;
    set_active(0);
    em_audio_ios_set_suspended(1);
    [UIApplication sharedApplication].idleTimerDisabled = NO;
}

- (void)sceneDidEnterBackground:(UIScene *)scene
{
    (void)scene;
    /* The frame in flight finishes and the thread parks before the app may
     * no longer use the GPU (a frame is ~17 ms; the bound is generous). */
    wait_parked(1.0);
}

- (void)dealloc
{
    [_window release];
    [super dealloc];
}
@end

@interface EmAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation EmAppDelegate
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)options
{
    (void)application;
    (void)options;
    launch_setup();
    return YES;
}

- (UISceneConfiguration *)application:(UIApplication *)application
    configurationForConnectingSceneSession:(UISceneSession *)session
                                   options:(UISceneConnectionOptions *)options
{
    (void)application;
    (void)options;
    UISceneConfiguration *config =
        [UISceneConfiguration configurationWithName:@"Default" sessionRole:session.role];
    config.delegateClass = [EmSceneDelegate class];
    return config;
}
@end

int main(int argc, char *argv[])
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, @"EmAppDelegate");
    }
}

/* ---- em_platform.h ------------------------------------------------------- */
EmWindow *em_window_create(const char *title, int width, int height)
{
    (void)title;
    (void)width;
    (void)height;
    if (!s_layer) return NULL;
    EmWindow *w = (EmWindow *)calloc(1, sizeof(EmWindow));
    if (w) w->layer = s_layer;
    return w;
}

void em_window_destroy(EmWindow *w)
{
    free(w);
}

bool em_window_poll(EmWindow *w, EmEvent *out)
{
    if (!w || !out) return false;
    /* Park while the app is not active (see the header comment). No key
     * events exist here: input comes from the controller (em_gamepad). */
    pthread_mutex_lock(&s_lock);
    if (!s_active) {
        s_parked = 1;
        pthread_cond_broadcast(&s_cond);
        while (!s_active) pthread_cond_wait(&s_cond, &s_lock);
        s_parked = 0;
    }
    pthread_mutex_unlock(&s_lock);
    return false;
}

void em_window_drawable_size(EmWindow *w, int *outW, int *outH)
{
    int width = w ? atomic_load(&s_drawable_w) : 0;
    int height = w ? atomic_load(&s_drawable_h) : 0;
    if (outW) *outW = width;
    if (outH) *outH = height;
}

void *em_window_native_handle(EmWindow *w)
{
    /* iOS: the CAMetalLayer itself (UIKit views are main-thread objects). */
    return w ? (void *)w->layer : NULL;
}
