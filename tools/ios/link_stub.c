/* link_stub.c — the Xcode target's only source file (docs/IOS.md).
 *
 * The whole program (game code, UIKit layer with main(), renderer, audio)
 * is compiled by `make ios-lib` into build/ios/<sdk>/libextermination.a,
 * which the target force-loads; Xcode links, bundles and signs it. A target
 * with no source of its own would not link at all, hence this file.
 */
typedef int em_ios_link_stub;
