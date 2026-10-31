/* em_module_loader.c's attachment scope call: no sound in the loader's fixtures. */
void em_sfx_set_attachment(int attachment);
void em_sfx_set_attachment(int attachment) { (void)attachment; }
