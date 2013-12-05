/*
 * "$Id$"
 *
 *   PDF to PostScript filter front-end for CUPS.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1997-2006 by Easy Software Products.
 *   Copyright 2011-2012 by Till Kamppeter
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 *   which should have been included with this file.  If this file is
 *   file is missing or damaged, see the license at "http://www.cups.org/".
 *
 * Contents:
 *
 *   main()       - Main entry for filter...
 *   cancel_job() - Flag the job as canceled.
 */

/*
 * Include necessary headers...
 */

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/file.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <config.h>

#define MAX_CHECK_COMMENT_LINES	20

/*
 * Local functions...
 */

static void		cancel_job(int sig);


/*
 * Local globals...
 */

static int		job_canceled = 0;
int			pdftopdfapplied = 0;
char			*deviceCopies = "1";
int			deviceCollate = 0;


/*
 * When calling the "pstops" filter we exclude the following options from its
 * command line as we have applied these options already to the PDF input,
 * either on the "pdftops"/Ghostscript call in this filter or by use of the
 * "pdftopdf" filter before this filter.
 */

const char *pstops_exclude_general[] = {
  "fitplot",
  "fit-to-page",
  "landscape",
  "orientation-requested",
  NULL
};

const char *pstops_exclude_page_management[] = {
  "brightness",
  "Collate",
  "cupsEvenDuplex",
  "gamma",
  "hue",
  "ipp-attribute-fidelity",
  "MirrorPrint",
  "mirror",
  "multiple-document-handling",
  "natural-scaling",
  "number-up",
  "number-up-layout",
  "OutputOrder",
  "page-border",
  "page-bottom",
  "page-label",
  "page-left",
  "page-ranges",
  "page-right",
  "page-set",
  "page-top",
  "position",
  "saturation",
  "scaling",
  NULL
};


/*
 * Check whether we were called after the "pdftopdf" filter and extract
 * parameters passed over by "pdftopdf" in the header comments of the PDF
 * file
 */

static void parsePDFTOPDFComment(char *filename)
{
  char buf[4096];
  int i;
  FILE *fp;

  if ((fp = fopen(filename,"rb")) == 0) {
    fprintf(stderr, "ERROR: pdftops - cannot open print file \"%s\"\n",
            filename);
    fclose(fp);
    return;
  }

  /* skip until PDF start header */
  while (fgets(buf,sizeof(buf),fp) != 0) {
    if (strncmp(buf,"%PDF",4) == 0) {
      break;
    }
  }
  for (i = 0;i < MAX_CHECK_COMMENT_LINES;i++) {
    if (fgets(buf,sizeof(buf),fp) == 0) break;
    if (strncmp(buf,"%%PDFTOPDFNumCopies",19) == 0) {
      char *p;

      p = strchr(buf+19,':') + 1;
      while (*p == ' ' || *p == '\t') p++;
      deviceCopies = strdup(p);
      pdftopdfapplied = 1;
    } else if (strncmp(buf,"%%PDFTOPDFCollate",17) == 0) {
      char *p;

      p = strchr(buf+17,':') + 1;
      while (*p == ' ' || *p == '\t') p++;
      if (strncasecmp(p,"true",4) == 0) {
	deviceCollate = 1;
      } else {
	deviceCollate = 0;
      }
      pdftopdfapplied = 1;
    } else if (strcmp(buf,"% This file was generated by pdftopdf") == 0) {
      pdftopdfapplied = 1;
    }
  }

  fclose(fp);
}


/*
 * Remove all options in option_list from the string option_str, including
 * option values after an "=" sign and preceded "no" before boolean options
 */

void remove_options(char *options_str, const char **option_list)
{
  const char	**option;		/* Option to be removed now */
  char		*option_start,		/* Start of option in string */
		*option_end;		/* End of option in string */

  for (option = option_list; *option; option ++)
  {
    while ((option_start = strcasestr(options_str, *option)) != NULL &&
	   (!option_start[strlen(*option)] ||
	    isspace(option_start[strlen(*option)] & 255) ||
	    option_start[strlen(*option)] == '='))
    {
      /*
       * Strip option...
       */

      option_end = option_start + strlen(*option);

      /* Remove preceding "no" of boolean option */
      if ((option_start - options_str) >= 2 &&
	  !strncasecmp(option_start - 2, "no", 2))
	option_start -= 2;

      /* Remove "=" and value */
      while (*option_end && !isspace(*option_end & 255))
	option_end ++;

      /* Remove spaces up to next option */
      while (*option_end && isspace(*option_end & 255))
	option_end ++;

      memmove(option_start, option_end, strlen(option_end) + 1);
    }
  }
}


/*
 * 'main()' - Main entry for filter...
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int		fd;			/* Copy file descriptor */
  char		*filename,		/* PDF file to convert */
		tempfile[1024];		/* Temporary file */
  char		buffer[8192];		/* Copy buffer */
  int		bytes;			/* Bytes copied */
  int		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */
  const char	*val;			/* Option value */
  int		orientation,		/* Output orientation */
		fit;			/* Fit output to default page size? */
  ppd_file_t	*ppd;			/* PPD file */
  ppd_size_t	*size;			/* Current page size */
  cups_file_t	*fp;			/* Post-processing input file */
  int		pdf_pid,		/* Process ID for pdftops */
		pdf_argc,		/* Number of args for pdftops */
		pstops_pid,		/* Process ID of pstops filter */
		pstops_pipe[2],		/* Pipe to pstops filter */
		need_post_proc = 0,     /* Post-processing needed? */
		post_proc_pid = 0,	/* Process ID of post-processing */
		post_proc_pipe[2],	/* Pipe to post-processing */
		wait_children,		/* Number of child processes left */
		wait_pid,		/* Process ID from wait() */
		wait_status,		/* Status from child */
		exit_status = 0;	/* Exit status */
  char		*pdf_argv[100],		/* Arguments for pdftops/gs */
		pdf_width[255],		/* Paper width */
		pdf_height[255],	/* Paper height */
		pstops_path[1024],	/* Path to pstops program */
		*pstops_argv[7],	/* Arguments for pstops filter */
		*pstops_options,	/* Options for pstops filter */
		*pstops_end;		/* End of pstops filter option */
  const char	*cups_serverbin;	/* CUPS_SERVERBIN environment variable */
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* Actions for POSIX signals */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */


 /*
  * Make sure status messages are not buffered...
  */

  setbuf(stderr, NULL);

 /*
  * Ignore broken pipe signals...
  */

  signal(SIGPIPE, SIG_IGN);

 /*
  * Make sure we have the right number of arguments for CUPS!
  */

  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "Usage: %s job user title copies options [file]\n",
	    argv[0]);
    return (1);
  }

 /*
  * Register a signal handler to cleanly cancel a job.
  */

#ifdef HAVE_SIGSET /* Use System V signals over POSIX to avoid bugs */
  sigset(SIGTERM, cancel_job);
#elif defined(HAVE_SIGACTION)
  memset(&action, 0, sizeof(action));

  sigemptyset(&action.sa_mask);
  action.sa_handler = cancel_job;
  sigaction(SIGTERM, &action, NULL);
#else
  signal(SIGTERM, cancel_job);
#endif /* HAVE_SIGSET */

 /*
  * Copy stdin if needed...
  */

  if (argc == 6)
  {
   /*
    * Copy stdin to a temp file...
    */

    if ((fd = cupsTempFd(tempfile, sizeof(tempfile))) < 0)
    {
      perror("DEBUG: Unable to copy PDF file");
      return (1);
    }

    fprintf(stderr, "DEBUG: pdftops - copying to temp print file \"%s\"\n",
            tempfile);

    while ((bytes = fread(buffer, 1, sizeof(buffer), stdin)) > 0)
      write(fd, buffer, bytes);

    close(fd);

    filename = tempfile;
  }
  else
  {
   /*
    * Use the filename on the command-line...
    */

    filename    = argv[6];
    tempfile[0] = '\0';
  }

 /*
  * Read out copy counts and collate setting passed over by pdftopdf
  */

  parsePDFTOPDFComment(filename);

 /*
  * Load the PPD file and mark options...
  */

  ppd         = ppdOpenFile(getenv("PPD"));
  num_options = cupsParseOptions(argv[5], 0, &options);

  ppdMarkDefaults(ppd);
  cupsMarkOptions(ppd, num_options, options);

 /*
  * Build the pstops command-line...
  */

  if ((cups_serverbin = getenv("CUPS_SERVERBIN")) == NULL)
    cups_serverbin = CUPS_SERVERBIN;

  snprintf(pstops_path, sizeof(pstops_path), "%s/filter/pstops",
           cups_serverbin);

  pstops_options = strdup(argv[5]);

  /*
   * Strip options which "pstops" does not need to apply any more
   */
  remove_options(pstops_options, pstops_exclude_general);
  if (pdftopdfapplied)
    remove_options(pstops_options, pstops_exclude_page_management);

  if (pdftopdfapplied && deviceCollate)
  {
   /*
    * Add collate option to the pstops call if pdftopdf has found out that the
    * printer does hardware collate.
    */

    pstops_options = realloc(pstops_options, sizeof(pstops_options) + 8);
    pstops_end = pstops_options + strlen(pstops_options);
    strcpy(pstops_end, " Collate");
  }

  pstops_argv[0] = argv[0];		/* Printer */
  pstops_argv[1] = argv[1];		/* Job */
  pstops_argv[2] = argv[2];		/* User */
  pstops_argv[3] = argv[3];		/* Title */
  if (pdftopdfapplied)
    pstops_argv[4] = deviceCopies;     	/* Copies */
  else
    pstops_argv[4] = argv[4];		/* Copies */
  pstops_argv[5] = pstops_options;	/* Options */
  pstops_argv[6] = NULL;

 /*
  * Build the command-line for the pdftops or gs filter...
  */

#ifdef HAVE_PDFTOPS
  pdf_argv[0] = (char *)"pdftops";
  pdf_argc    = 1;
#else
  pdf_argv[0] = (char *)"gs";
  pdf_argv[1] = (char *)"-q";
  pdf_argv[2] = (char *)"-dNOPAUSE";
  pdf_argv[3] = (char *)"-dBATCH";
  pdf_argv[4] = (char *)"-dSAFER";
#  ifdef HAVE_GHOSTSCRIPT_PS2WRITE
  pdf_argv[5] = (char *)"-sDEVICE=ps2write";
#  else
  pdf_argv[5] = (char *)"-sDEVICE=pswrite";
#  endif /* HAVE_GHOSTSCRIPT_PS2WRITE */
  pdf_argv[6] = (char *)"-sOUTPUTFILE=%stdout";
  pdf_argc    = 7;
#endif /* HAVE_PDFTOPS */

  if (ppd)
  {
   /*
    * Set language level and TrueType font handling...
    */

    if (ppd->language_level == 1)
    {
#ifdef HAVE_PDFTOPS
      pdf_argv[pdf_argc++] = (char *)"-level1";
      pdf_argv[pdf_argc++] = (char *)"-noembtt";
#else
      pdf_argv[pdf_argc++] = (char *)"-dLanguageLevel=1";
#endif /* HAVE_PDFTOPS */
    }
    else if (ppd->language_level == 2)
    {
#ifdef HAVE_PDFTOPS
      pdf_argv[pdf_argc++] = (char *)"-level2";
      if (!ppd->ttrasterizer)
	pdf_argv[pdf_argc++] = (char *)"-noembtt";
#else
      pdf_argv[pdf_argc++] = (char *)"-dLanguageLevel=2";
#endif /* HAVE_PDFTOPS */
    }
    else
#ifdef HAVE_PDFTOPS
      /* Do not emit PS Level 3 with Poppler, some HP PostScript printers
         do not like it. See https://bugs.launchpad.net/bugs/277404. */
      pdf_argv[pdf_argc++] = (char *)"-level2";
#else
      pdf_argv[pdf_argc++] = (char *)"-dLanguageLevel=3";
#endif /* HAVE_PDFTOPS */

    if ((val = cupsGetOption("fitplot", num_options, options)) == NULL)
      val = cupsGetOption("fit-to-page", num_options, options);

    if (val && strcasecmp(val, "no") && strcasecmp(val, "off") &&
	strcasecmp(val, "false"))
      fit = 1;
    else
      fit = 0;

   /*
    * Set output page size...
    */

    size = ppdPageSize(ppd, NULL);
    if (size && fit)
    {
     /*
      * Got the size, now get the orientation...
      */

      orientation = 0;

      if ((val = cupsGetOption("landscape", num_options, options)) != NULL)
      {
	if (strcasecmp(val, "no") != 0 && strcasecmp(val, "off") != 0 &&
	    strcasecmp(val, "false") != 0)
	  orientation = 1;
      }
      else if ((val = cupsGetOption("orientation-requested", num_options,
                                    options)) != NULL)
      {
       /*
	* Map IPP orientation values to 0 to 3:
	*
	*   3 = 0 degrees   = 0
	*   4 = 90 degrees  = 1
	*   5 = -90 degrees = 3
	*   6 = 180 degrees = 2
	*/

	orientation = atoi(val) - 3;
	if (orientation >= 2)
	  orientation ^= 1;
      }

#ifdef HAVE_PDFTOPS
      if (orientation & 1)
      {
	snprintf(pdf_width, sizeof(pdf_width), "%.0f", size->length);
	snprintf(pdf_height, sizeof(pdf_height), "%.0f", size->width);
      }
      else
      {
	snprintf(pdf_width, sizeof(pdf_width), "%.0f", size->width);
	snprintf(pdf_height, sizeof(pdf_height), "%.0f", size->length);
      }

      pdf_argv[pdf_argc++] = (char *)"-paperw";
      pdf_argv[pdf_argc++] = pdf_width;
      pdf_argv[pdf_argc++] = (char *)"-paperh";
      pdf_argv[pdf_argc++] = pdf_height;
      pdf_argv[pdf_argc++] = (char *)"-expand";

#else
      if (orientation & 1)
      {
	snprintf(pdf_width, sizeof(pdf_width), "-dDEVICEWIDTHPOINTS=%.0f",
	         size->length);
	snprintf(pdf_height, sizeof(pdf_height), "-dDEVICEHEIGHTPOINTS=%.0f",
	         size->width);
      }
      else
      {
	snprintf(pdf_width, sizeof(pdf_width), "-dDEVICEWIDTHPOINTS=%.0f",
	         size->width);
	snprintf(pdf_height, sizeof(pdf_height), "-dDEVICEHEIGHTPOINTS=%.0f",
	         size->length);
      }

      pdf_argv[pdf_argc++] = pdf_width;
      pdf_argv[pdf_argc++] = pdf_height;
#endif /* HAVE_PDFTOPS */
    }
#if defined(HAVE_PDFTOPS) && defined(HAVE_PDFTOPS_WITH_ORIGPAGESIZES)
    else
    {
     /*
      *  Use the page sizes of the original PDF document, this way documents
      *  which contain pages of different sizes can be printed correctly
      */

      pdf_argv[pdf_argc++] = (char *)"-origpagesizes";
    }
#endif /* HAVE_PDFTOPS && HAVE_PDFTOPS_WITH_ORIGPAGESIZES */
  }

#ifdef HAVE_PDFTOPS
  pdf_argv[pdf_argc++] = filename;
  pdf_argv[pdf_argc++] = (char *)"-";
#else
 /*
  * PostScript debug mode: If you send a job with "lpr -o psdebug" Ghostscript
  * will not compress pages and fonts, so that the PostScript code can get
  * analysed. This is especially important if a PostScript printer errors or
  * misbehaves on Ghostscript's output.
  */
  val = cupsGetOption("psdebug", num_options, options);
  if (val && strcasecmp(val, "no") && strcasecmp(val, "off") &&
      strcasecmp(val, "false"))
  {
    fprintf(stderr, "DEBUG: Deactivated compression of pages and fonts in Ghostscript's PostScript output (\"psdebug\" debug mode)\n");
    pdf_argv[pdf_argc++] = (char *)"-dCompressPages=false";
    pdf_argv[pdf_argc++] = (char *)"-dCompressFonts=false";
  }
 /*
  * The PostScript interpreters on Brother printers (BR-Script) have a bug in
  * their CCITTFaxDecode filter. So we do not CCITT-compress bitmap glyphs and
  * images if the PostScript is for a Brother printer.
  */
  if (ppd && ppd->manufacturer &&
      !strncasecmp(ppd->manufacturer, "Brother", 7))
  {
    fprintf(stderr, "DEBUG: Deactivated CCITT compression of glyphs and images as workaround for Brother printers\n");
    pdf_argv[pdf_argc++] = (char *)"-dNoT3CCITT";
    pdf_argv[pdf_argc++] = (char *)"-dEncodeMonoImages=false";
  }
  pdf_argv[pdf_argc++] = (char *)"-c";
  pdf_argv[pdf_argc++] = (char *)"save pop";
  pdf_argv[pdf_argc++] = (char *)"-f";
  pdf_argv[pdf_argc++] = filename;
#endif /* HAVE_PDFTOPS */

  pdf_argv[pdf_argc] = NULL;

 /*
  * Do we need post-processing of the PostScript output to work around bugs
  * of the printer's PostScript interpreter?
  */

#ifdef HAVE_PDFTOPS
  need_post_proc = 0;
#else
  need_post_proc =
    (ppd && ppd->manufacturer &&
     (!strncasecmp(ppd->manufacturer, "Kyocera", 7) ||
      !strncasecmp(ppd->manufacturer, "Brother", 7)) ? 1 : 0);
#endif /* HAVE_PDFTOPS */

 /*
  * Execute "pdftops/gs | pstops [ | post-processing ]"...
  */

  if (pipe(pstops_pipe))
  {
    perror("DEBUG: Unable to create pipe for pstops");

    exit_status = 1;
    goto error;
  }

  if (need_post_proc)
  {
    if (pipe(post_proc_pipe))
    {
      perror("DEBUG: Unable to create pipe for post-processing");

      exit_status = 1;
      goto error;
    }
  }

  if ((pdf_pid = fork()) == 0)
  {
   /*
    * Child comes here...
    */

    if (need_post_proc)
    {
      dup2(post_proc_pipe[1], 1);
      close(post_proc_pipe[0]);
      close(post_proc_pipe[1]);
    }
    else
      dup2(pstops_pipe[1], 1);
    close(pstops_pipe[0]);
    close(pstops_pipe[1]);

#ifdef HAVE_PDFTOPS
    execv(CUPS_PDFTOPS, pdf_argv);
    perror("DEBUG: Unable to execute pdftops program");
#else
    execv(CUPS_GHOSTSCRIPT, pdf_argv);
    perror("DEBUG: Unable to execute gs program");
#endif /* HAVE_PDFTOPS */

    exit(1);
  }
  else if (pdf_pid < 0)
  {
   /*
    * Unable to fork!
    */

#ifdef HAVE_PDFTOPS
    perror("DEBUG: Unable to execute pdftops program");
#else
    perror("DEBUG: Unable to execute gs program");
#endif /* HAVE_PDFTOPS */

    exit_status = 1;
    goto error;
  }

  fprintf(stderr, "DEBUG: Started filter %s (PID %d)\n", pdf_argv[0], pdf_pid);

  if (need_post_proc)
  {
    if ((post_proc_pid = fork()) == 0)
    {
     /*
      * Child comes here...
      */

      dup2(post_proc_pipe[0], 0);
      close(post_proc_pipe[0]);
      close(post_proc_pipe[1]);
      dup2(pstops_pipe[1], 1);
      close(pstops_pipe[0]);
      close(pstops_pipe[1]);

      fp = cupsFileStdin();

     /*
      * Copy everything until after initial comments (Prolog section)
      */
      while ((bytes = cupsFileGetLine(fp, buffer, sizeof(buffer))) > 0 &&
	     strncmp(buffer, "%%BeginProlog", 13) &&
	     strncmp(buffer, "%%EndProlog", 11) &&
	     strncmp(buffer, "%%BeginSetup", 12) &&
	     strncmp(buffer, "%%Page:", 7))
	printf("%s", buffer);

      if (bytes > 0)
      {
       /*
	* Insert PostScript interpreter bug fix code in the beginning of
	* the Prolog section (before the first active PostScript code)
	*/
	if (strncmp(buffer, "%%BeginProlog", 13))
	{
	  /* No Prolog section, create one */
	  fprintf(stderr, "DEBUG: Adding Prolog section for workaround PostScript code\n");
	  puts("%%BeginProlog");
	}
	else
	  printf("%s", buffer);
	
#ifndef HAVE_PDFTOPS

	if (ppd && ppd->manufacturer)
	{

	 /*
	  * Kyocera printers have a bug in their PostScript interpreter 
	  * making them crashing on PostScript input data generated by
	  * Ghostscript's "ps2write" output device.
	  *
	  * The problem can be simply worked around by preceding the PostScript
	  * code with some extra bits.
	  *
	  * See https://bugs.launchpad.net/bugs/951627
	  */

	  if (!strncasecmp(ppd->manufacturer, "Kyocera", 7))
	  {
	    fprintf(stderr, "DEBUG: Inserted workaround PostScript code for Kyocera printers\n");
	    puts("% ===== Workaround insertion by pdftops CUPS filter =====");
	    puts("% Kyocera's PostScript interpreter crashes on early name binding,");
	    puts("% so eliminate all \"bind\"s by redifining \"bind\" to no-op");
	    puts("/bind {} bind def");
	    puts("% =====");
	  }

	 /*
	  * Brother printers have a bug in their PostScript interpreter 
	  * making them printing one blank page if PostScript input data
	  * generated by Ghostscript's "ps2write" output device is used.
	  *
	  * The problem can be simply worked around by preceding the PostScript
	  * code with some extra bits.
	  *
	  * See https://bugs.launchpad.net/bugs/950713
	  */

	  else if (!strncasecmp(ppd->manufacturer, "Brother", 7))
	  {
	    fprintf(stderr, "DEBUG: Inserted workaround PostScript code for Brother printers\n");
	    puts("% ===== Workaround insertion by pdftops CUPS filter =====");
	    puts("% Brother's PostScript interpreter spits out the current page");
	    puts("% and aborts the job on the \"currenthalftone\" operator, so redefine");
	    puts("% it to null");
	    puts("/currenthalftone {//null} bind def");
	    puts("/orig.sethalftone systemdict /sethalftone get def");
	    puts("/sethalftone {dup //null eq not {//orig.sethalftone}{pop} ifelse} bind def");
	    puts("% =====");
	  }
	}
	
#endif /* !HAVE_PDFTOPS */

	if (strncmp(buffer, "%%BeginProlog", 13))
	{
	  /* Close newly created Prolog section */
	  if (strncmp(buffer, "%%EndProlog", 11))
	    puts("%%EndProlog");
	  printf("%s", buffer);
	}

       /*
	* Copy the rest of the file
	*/
	while ((bytes = cupsFileRead(fp, buffer, sizeof(buffer))) > 0)
	  fwrite(buffer, 1, bytes, stdout);

      }

      exit(0);
    }
    else if (post_proc_pid < 0)
    {
     /*
      * Unable to fork!
      */

      perror("DEBUG: Unable to execute post-processing process");

      exit_status = 1;
      goto error;
    }

    fprintf(stderr, "DEBUG: Started post-processing (PID %d)\n", post_proc_pid);
  }

  if ((pstops_pid = fork()) == 0)
  {
   /*
    * Child comes here...
    */

    if (need_post_proc)
    {
      close(post_proc_pipe[0]);
      close(post_proc_pipe[1]);
    }
    dup2(pstops_pipe[0], 0);
    close(pstops_pipe[0]);
    close(pstops_pipe[1]);

    execv(pstops_path, pstops_argv);
    perror("DEBUG: Unable to execute pstops program");

    exit(1);
  }
  else if (pstops_pid < 0)
  {
   /*
    * Unable to fork!
    */

    perror("DEBUG: Unable to execute pstops program");

    exit_status = 1;
    goto error;
  }

  fprintf(stderr, "DEBUG: Started filter pstops (PID %d)\n", pstops_pid);

  close(pstops_pipe[0]);
  close(pstops_pipe[1]);
  if (need_post_proc)
  {
    close(post_proc_pipe[0]);
    close(post_proc_pipe[1]);
  }

 /*
  * Wait for the child processes to exit...
  */

  wait_children = 2 + need_post_proc;

  while (wait_children > 0)
  {
   /*
    * Wait until we get a valid process ID or the job is canceled...
    */

    while ((wait_pid = wait(&wait_status)) < 0 && errno == EINTR)
    {
      if (job_canceled)
      {
	kill(pdf_pid, SIGTERM);
	if (need_post_proc)
	  kill(post_proc_pid, SIGTERM);
	kill(pstops_pid, SIGTERM);

	job_canceled = 0;
      }
    }

    if (wait_pid < 0)
      break;

    wait_children --;

   /*
    * Report child status...
    */

    if (wait_status)
    {
      if (WIFEXITED(wait_status))
      {
	exit_status = WEXITSTATUS(wait_status);

	fprintf(stderr, "DEBUG: PID %d (%s) stopped with status %d!\n",
		wait_pid,
#ifdef HAVE_PDFTOPS
		wait_pid == pdf_pid ? "pdftops" :
#else
		wait_pid == pdf_pid ? "gs" :
#endif /* HAVE_PDFTOPS */
		(wait_pid == pstops_pid ? "pstops" : "Post-processing"),
		exit_status);
      }
      else if (WTERMSIG(wait_status) == SIGTERM)
      {
	fprintf(stderr,
		"DEBUG: PID %d (%s) was terminated normally with signal %d!\n",
		wait_pid,
#ifdef HAVE_PDFTOPS
		wait_pid == pdf_pid ? "pdftops" :
#else
		wait_pid == pdf_pid ? "gs" :
#endif /* HAVE_PDFTOPS */
		(wait_pid == pstops_pid ? "pstops" : "Post-processing"),
		exit_status);
      }
      else
      {
	exit_status = WTERMSIG(wait_status);

	fprintf(stderr, "DEBUG: PID %d (%s) crashed on signal %d!\n", wait_pid,
#ifdef HAVE_PDFTOPS
		wait_pid == pdf_pid ? "pdftops" :
#else
		wait_pid == pdf_pid ? "gs" :
#endif /* HAVE_PDFTOPS */
		(wait_pid == pstops_pid ? "pstops" : "Post-processing"),
		exit_status);
      }
    }
    else
    {
      fprintf(stderr, "DEBUG: PID %d (%s) exited with no errors.\n", wait_pid,
#ifdef HAVE_PDFTOPS
	      wait_pid == pdf_pid ? "pdftops" :
#else
	      wait_pid == pdf_pid ? "gs" :
#endif /* HAVE_PDFTOPS */
	      (wait_pid == pstops_pid ? "pstops" : "Post-processing"));
    }
  }

 /*
  * Cleanup and exit...
  */

  error:

  if (tempfile[0])
    unlink(tempfile);

  return (exit_status);
}


/*
 * 'cancel_job()' - Flag the job as canceled.
 */

static void
cancel_job(int sig)			/* I - Signal number (unused) */
{
  (void)sig;

  job_canceled = 1;
}


/*
 * End of "$Id$".
 */
