/* Two closure symbols never reached in a single-byte, no-float console demo:
   _ismbblead -> single-byte CP/M console has no multibyte lead bytes (return 0);
   __fatal_runtime_error -> only invoked by the noefgfmt float stub, which we
   never hit (no %e/%f/%g). A real full libc would supply the true versions. */
int _ismbblead( unsigned int c ) { (void)c; return 0; }
void __fatal_runtime_error( const char *msg ) { (void)msg; for( ;; ) ; }
