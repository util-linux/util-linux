#ifdef lint
   static char RCSid[] = "dsplit.c,v 1.1.1.1 1995/02/22 19:09:14";
#endif
/*
   Program dsplit:  Splits a large  file into pieces.

   Usage:
        dsplit [-size nnn] [input_file [output_base]]
   Size        is size of each output file, in bytes.  The default is 1457000,
	       enough to fill a "1.44MB" diskette, save 152 bytes.
   input_file  is the name of the file to split up.  A dash (-) indicates 
                  standard input.  Defaults to standard input.
   output_base is the name of the output files to be written, minus the
                  extension.  Dsplit adds suffixes 000, 001, ...
                  The default base name is dsplit.
*/
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if (defined (__MSDOS__) || defined (WIN32))
#include <io.h>
#include <fcntl.h>
#endif /* __MSDOS__ or WIN32 */
#ifndef FILENAME_MAX
#define FILENAME_MAX 1024
#endif

#define DEFAULT_NAME "dsplit"
#define DEFAULT_SIZE 1457000L
#if (defined (__MSDOS__) || defined (WIN32))
#   define READ_OPTIONS  "rb"
#   define WRITE_OPTIONS "wb"
#else
#   define READ_OPTIONS  "r"
#   define WRITE_OPTIONS "w"
#endif /* __MSDOS__ or WIN32 */

#ifndef MIN
#define MIN(a,b) (((a) <= (b)) ? (a) : (b))
#endif

static unsigned long output_size = DEFAULT_SIZE;
static char* base_name = DEFAULT_NAME;
static FILE* input_handle;
static char* input_name = "-";

#ifdef __STDC__
static void init (int argc, char* argv[]);
static int write_one (int count);
static void ToLower (char* string);
static void usage_error (void);
#else /* not __STDC__ */
static void init (/* int argc, char* argv[] */);
static int write_one (/* int count */);
static void ToLower (/* char* string */);
static void usage_error (/* void */);
#endif /* __STDC__ */



#ifdef __STDC__
int main (int argc, char* argv[])
#else
int main (argc, argv)
int argc;
char* argv[];
#endif
{
   int count;

   /* Process command line arguments, open input file. */
   init (argc, argv);

   /* Write the output files */
   for (count = 0 ; write_one (count) ; ++count)
      ;

   /* Close input file (a matter of style) */
   if (fclose (input_handle) != 0)
   {
      (void)fprintf (stderr, "Could not close file \"%s\" for input\n",
	 input_name);
      return 1;
   }

   /* Normal successful conclusion */
   return 0;
}



#ifdef __STDC__
static void init (int argc, char* argv[])
#else
static void init (argc, argv)
int argc;
char* argv[];
#endif
{
   int iarg;
   int name_count;

   /* Initialize the input file handle to standard input.  IBM's Toolset++
   won't let me do this statically, unfortunately. */
   input_handle = stdin;

   /* Initialize for following loop */
   name_count = 0;

   /* Loop over command line arguments */
   for (iarg = 1 ; iarg < argc ; ++iarg)
   {
      /* Point to argument,for convenience */
      char* arg = argv[iarg];

      /* If this argument is an option */
      if (arg[0] == '-' && arg[1] != '\0')
      {
         /* Process option if recognized */
         ToLower (arg+1);
         if (strcmp (arg+1, "size") != 0)
            usage_error ();
	 ++iarg;
	 if (iarg >= argc)
	    usage_error ();
	 arg = argv[iarg];
	 if (sscanf (arg, "%ld", &output_size) != 1)
	 {
	    (void)fprintf (stderr, "Illegal numeric expression \"%s\"\n", arg);
	    exit (1);
	 }
      } 
      else /* argument is not an option */
      {
         /* Argument is a name string.  Determine which one. */
         switch (name_count)
         {
         case 0:
            input_name = argv[iarg];
            break;
	 case 1:
            base_name = argv[iarg];
	    break;
	 default:
	    usage_error ();
	    break;
         }
         ++name_count;

      } /* End if this argument is an option */

   }  /* End loop over command line arguments */

   /* Open the input file */
   if (strcmp (input_name, "-") == 0)
   {
#  if (defined (__MSDOS__) || defined (WIN32))
      if (setmode (0, O_BINARY) == -1)
      {
         perror ("dsplit: setmode");
         exit (1);
      }
#  endif
   }
   else
   {
      if ((input_handle = fopen (input_name, READ_OPTIONS)) == NULL)
      {
	 (void)fprintf (stderr, "Could not open file \"%s\" for input\n",
	    input_name);
	 exit (1);
      }
   }
}



#ifdef __STDC__
static int write_one (int count)
#else
static int write_one (count)
int count;
#endif
{
   char output_name[FILENAME_MAX];
   int bytes_written;
   unsigned long total_written;
   FILE* output_handle;

   /* Read the first buffer full now, just to see if any data is left */
   static char buff[1024];
   int to_read = MIN (sizeof(buff), output_size);
   int bytes_read = fread (buff, 1, to_read, input_handle);
   if (bytes_read <= 0)
      return 0;

   /* Open file for output */
   sprintf (output_name, "%s.%03d", base_name, count);
   output_handle = fopen (output_name, WRITE_OPTIONS);
   if (output_handle == NULL)
   {
      (void)fprintf (stderr,
         "Could not open file \"%s\" for output\n", output_name);
      exit (1);
   }

   /* Write the first output buffer */
   bytes_written = fwrite (buff, 1, bytes_read, output_handle);
   if (bytes_written != bytes_read)
   {
      (void)fprintf (stderr, "Error writing to file \"%s\"\n", output_name);
      exit (1);
   }
   total_written = bytes_written;

   /* Write output file */
   while (total_written < output_size)
   {
      to_read = MIN (sizeof(buff), output_size-total_written);
      bytes_read = fread (buff, 1, to_read, input_handle);
      if (bytes_read <= 0)
         break;
      bytes_written = fwrite (buff, 1, bytes_read, output_handle);
      if (bytes_written != bytes_read)
      {
         (void)fprintf (stderr, "Error writing to file \"%s\"\n", output_name);
         exit (1);
      }
      total_written += bytes_written;
   }

   /* Close the output file, it is complete */
   if (fclose (output_handle) != 0)
   {
      (void)fprintf (stderr,
         "Could not close file \"%s\" for output\n", output_name);
      exit (1);
   }

   /* Indicate whether more data remains to be transferred */
   return (bytes_read == to_read);
}



#ifdef __STDC__
static void ToLower (char* string)
#else
static void ToLower (string)
char* string;
#endif
{
   
   while (*string != '\0')
      tolower (*string++);
}



#ifdef __STDC__
static void usage_error (void)
#else
static void usage_error ()
#endif
{
   (void)fprintf (stderr, 
      "Usage: dsplit [-size nnn] [input_file [output_base]]\n");
   exit (1);
}

