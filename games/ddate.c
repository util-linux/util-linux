/* ddate.c .. converts boring normal dates to fun Discordian Date -><-
   written  the 65th day of The Aftermath in the Year of Our Lady of 
   Discord 3157 by Druel the Chaotic aka Jeremy Johnson aka
   mpython@gnu.ai.mit.edu  

   and I'm not responsible if this program messes anything up (except your 
   mind, I'm responsible for that)

   Modifications for Unix by Lee Harvey Oswald Smith, K.S.C.
   Five tons of flax.
*/

#include <time.h>
#include <string.h>
#include <stdio.h>

struct disc_time
{int season; /* 0-4 */
 int day; /* 0-72 */
 int yday; /* 0-365 */
 int year; /* 3066- */
};

char *ending(int);
void print(struct disc_time,char **);
struct disc_time convert(int,int);
struct disc_time makeday(int,int,int);

main (int argc,char **argv) 
{long t;
 struct tm *eris;
 int bob,raw;
 struct disc_time hastur;
 if (argc==4)
    { int moe,larry,curly;
      moe=atoi(argv[1]);
      larry=atoi(argv[2]);
      curly=atoi(argv[3]);
      hastur=makeday(moe,larry,curly);
    }
  else if (argc!=1)
    { fprintf(stderr,"Syntax: DiscDate [month day year]");
      exit(1);
    }
  else
    {
      t= time(NULL);
      eris=localtime(&t);
      bob=eris->tm_yday; /* days since Jan 1. */
      raw=eris->tm_year; /* years since 1980 */
      hastur=convert(bob,raw);
    }
      print(hastur,argv);
}

struct disc_time makeday(int imonth,int iday,int iyear) /*i for input */
{ struct disc_time funkychickens;
  
  int cal[12] = 
    {
       31,28,31,30,31,30,31,31,30,31,30,31
    };
  int dayspast=0;

  imonth--;
  funkychickens.year= iyear+1166;
  while(imonth>0)
     {
       dayspast+=cal[--imonth];
     }
  funkychickens.day=dayspast+iday-1;
  funkychickens.season=0;
   if((funkychickens.year%4)==2)
     {
       if (funkychickens.day==59)
         funkychickens.day=-1;
     }
  funkychickens.yday=funkychickens.day;
/*               note: EQUAL SIGN...hopefully that fixes it */
  while(funkychickens.day>=73)
      {
	funkychickens.season++;
	funkychickens.day-=73;
      }
  return funkychickens;
}

char *ending(int num)
{  
 int temp;
 char *funkychickens;
 
 funkychickens=(char *)malloc(sizeof(char)*3);
 
  temp=num%10; /* get 0-9 */  
  switch (temp)
  { case 1:
      strcpy(funkychickens,"st");
      break;
    case 2:
      strcpy(funkychickens,"nd");
      break;
    case 3:
      strcpy(funkychickens,"rd");
      break;
    default:
      strcpy(funkychickens,"th");
    }
 return funkychickens;
}

struct disc_time convert(int nday, int nyear)
{  struct disc_time funkychickens;
   
   funkychickens.year = nyear+3066;
   funkychickens.day=nday;
   funkychickens.season=0;
   if ((funkychickens.year%4)==2)
     {if (funkychickens.day==59)
	funkychickens.day=-1;
     else if (funkychickens.day >59)
       funkychickens.day-=1;
    }
   funkychickens.yday=funkychickens.day;
   while (funkychickens.day>=73)
     { funkychickens.season++;
       funkychickens.day-=73;
     }
   return funkychickens;
  
 }

void print(struct disc_time tick, char **args)
{ char *days[5] = { "Sweetmorn",
		    "Boomtime",
		    "Pungenday",
		    "Prickle-Prickle",
		    "Setting Orange"
		  };
  char *seasons[5] = { "Chaos",
		       "Discord",
		       "Confusion",
		       "Bureaucracy",
		       "The Aftermath"
                     };
  char *holidays[5][2] = { "Mungday", "Chaoflux",
			   "Mojoday", "Discoflux",
			   "Syaday",  "Confuflux",
			   "Zaraday", "Bureflux",
			   "Maladay", "Afflux"
			 };
  if (args[1]==NULL)
    printf("Today is ");
  else
    printf("%s-%s-%s is ",args[1],args[2],args[3]);
  if (tick.day==-1) printf("St. Tib's Day!");
  else
    { tick.day++;
      printf("%s",days[tick.yday%5]);
      printf(", the %d", tick.day);
      printf("%s day of %s",ending(tick.day),seasons[tick.season]) ;
    }
  printf(" in the YOLD %d\n",tick.year);
  if ((tick.day==5)||(tick.day==50))
    { printf("Celebrate ");
      if (tick.day==5)
	printf("%s\n",holidays[tick.season][0]);
      else
	printf("%s\n",holidays[tick.season][1]);
    }
}
