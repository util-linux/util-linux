/* defined in login.c */
extern void badlogin(const char *s);
extern void sleepexit(int);
extern struct hostent hostaddress;
extern char *hostname;

/* defined in checktty.c */
extern void checktty(const char *user, const char *tty, struct passwd *pwd);
