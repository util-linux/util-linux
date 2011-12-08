/*
    i18nized by:  KUN-CHUNG, HSIEH <linuxer@coventive.com>
		  Taiwan

    Homepage: http://www.geocities.com/linux4tw/

    程式國際化設計:  謝崑中
*/

#ifndef __i18n__
   #define __i18n__
   #define PKG "eject"
   #define LOCALEDIR "/usr/share/locale"

   #include <locale.h>
   #include <libintl.h>
   #define _(str) gettext (str)
   #define N_(str) (str)
   #define I18NCODE setlocale(LC_ALL,""); textdomain(PKG); bindtextdomain(PKG,LOCALEDIR);

   void i18n_init (void);
#endif
