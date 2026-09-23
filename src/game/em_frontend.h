/* Native resource/rendering adapter for the recovered startup control flow. */
#ifndef EM_FRONTEND_H
#define EM_FRONTEND_H

#include <stdint.h>

void em_frontend_install(void);
void em_frontend_shutdown(void);
int em_frontend_failed(void);

/* The game task's movie request (S12a): 001AD360 step 1 stores the movie
 * selector D_00275C78 = 0 and then D_00821058 = 1, which the main loop's
 * blocking movie driver 00203350 serves at step M (em_frame.c). The native
 * frontend owns the movie player, so the two stores land here. Only selector
 * 0 (E900.PSS, assets/startup/intro.mov) is exported; any other selector, or
 * a D_00821058 value other than 1, is refused (-1, fail-stop). */
int em_frontend_movie_select(uint8_t selector); /* D_00275C78 */
int em_frontend_movie_request(uint8_t value);   /* D_00821058 */

#endif
