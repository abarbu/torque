/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/
/*
 * save_attr.c - This file contains the functions to perform a buffered
 * save of an object (structure) and an attribute array to a file.
 * It also has the function to recover (reload) an attribute array.
 *
 * Included public functions are:
 *
 * save_setup called to initialize the buffer
 * save_struct copy a struct into the save i/o buffer
 * save_flush flush out the current save operation
 * save_attr buffer and write attributes to disk file
 * recov_attr read attributes from disk file
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "pbs_ifl.h"
#include <assert.h>
#include <errno.h>
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "list_link.h"
#include "attribute.h"
#include "log.h"
#include "svrfunc.h"
#include "utils.h"
#include "server.h"
#include "dynamic_string.h"


/* Global Variables */

extern int resc_access_perm;

/* data items global to functions in this file */

#define PKBUFSIZE 2048
#define ENDATTRIBUTES -711

char   pk_buffer[PKBUFSIZE]; /* used to do buffered output */
static int     pkbfds = -2; /* descriptor to use for saves */
static size_t  spaceavail; /* space in pk_buffer available */
static size_t  spaceused = 0; /* amount of space used  in pkbuffer */


/*
 * save_setup - set up the save i/o buffer.
 * The "buffer control information" is left updated to reflect
 * the file descriptor, and the space in the buffer.
 */

void save_setup(

  int fds)  /* file descriptor to use for save */

  {
  if (pkbfds != -2)
    {
    /* somebody forgot to flush the buffer */

    log_err(-1, "save_setup", "someone forgot to flush");
    }

  /* initialize buffer control */

  pkbfds = fds;

  spaceavail = PKBUFSIZE;

  spaceused = 0;

  return;
  }  /* END save_setup() */




/*
 * save_struct - Copy a structure (as a block)  into the save i/o buffer
 * This is useful to save fixed sized structure without pointers
 * that point outside of the structure itself.
 *
 * Write out buffer as required. Leave spaceavail and spaceused updated
 *
 * Returns: 0 on success
 *  -1 on error
 */

int save_struct(

  char         *pobj,    /* I */
  unsigned int  objsize) /* I */

  {
  int    amt;
  size_t copysize;
  int    i;
  char  *pbufin;
  char  *pbufout;

  assert(pkbfds >= 0);

  /* NOTE:  pkbfds, spaceavail, spaceused, and pk_buffer are global */

  while (objsize > 0)
    {
    pbufin = pk_buffer + spaceused;

    if (objsize > spaceavail)
      {
      copysize = spaceavail;

      if (copysize != 0)
        {
        memcpy(pbufin, pobj, copysize);
        }

      amt = PKBUFSIZE;

      pbufout = pk_buffer;

      while ((i = write(pkbfds, pbufout, amt)) != amt)
        {
        if (i == -1)
          {
          if (errno != EINTR)
            {
            return(-1);
            }
          }
        else
          {
          amt -= i;
          pbufout += i;
          }
        }

      pobj += copysize;

      spaceavail = PKBUFSIZE;
      spaceused  = 0;
      }
    else
      {
      copysize = (size_t)objsize;

      memcpy(pbufin, pobj, copysize);

      spaceavail -= copysize;
      spaceused  += copysize;
      }

    objsize -= copysize;
    }  /* while (objsize > 0) */

  return(0);
  }  /* END save_struct() */





/*
 * save_flush - flush out the current save operation
 * Flush buffer if needed, reset spaceavail, spaceused,
 * clear out file descriptor
 *
 * Returns: 0 on success
 *  -1 on failure (flush failed)
 */

int save_flush(void)

  {
  int   i;
  char *pbuf;

  /* NOTE:  spaceused, pkbfds, and pk_buffer are global */

  assert(pkbfds >= 0);

  pbuf = pk_buffer;

  if (spaceused > 0)
    {
    while ((i = write(pkbfds, pbuf, spaceused)) != (ssize_t)spaceused)
      {
      if (i == -1)
        {
        if (errno != EINTR)
          {
          log_err(errno, "save_flush", "bad write");

          return(-1);
          }
        }
      else
        {
        pbuf      += i;
        spaceused -= i;
        }
      }
    }

  pkbfds = -2; /* flushed flag */

  return(0);
  }  /* END save_flush() */





/*
 * save_attr() - write set of attributes to disk file
 *
 * Each of the attributes is encoded  into the attrlist form.
 * They are packed and written using save_struct().
 *
 * The final real attribute is followed by a dummy attribute with a
 * al_size of ENDATTRIB.  This cannot be mistaken for the size of a
 * real attribute.
 *
 * Note: attributes of type ATR_TYPE_ACL are not saved with the other
 * attribute of the parent (queue or server).  They are kept in their
 * own file.
 */

int save_attr(

  struct attribute_def *padef,   /* attribute definition array */
  struct attribute     *pattr,   /* ptr to attribute value array */
  int                   numattr) /* number of attributes in array */

  {
  svrattrl  dummy;
  int   errct = 0;
  tlist_head   lhead;
  int   i;
  svrattrl *pal;
  int   rc;

  /* encode each attribute which has a value (not non-set) */

  CLEAR_HEAD(lhead);

  for (i = 0;i < numattr;i++)
    {
    if ((padef + i)->at_type != ATR_TYPE_ACL)
      {
      /* NOTE: access lists are not saved this way */

      rc = (padef + i)->at_encode(
             pattr + i,
             &lhead,
             (padef + i)->at_name,
             NULL,
             ATR_ENCODE_SAVE);

      if (rc < 0)
        errct++;

      (pattr + i)->at_flags &= ~ATR_VFLAG_MODIFY;

      /* now that it has been encoded, block and save it */

      while ((pal = (svrattrl *)GET_NEXT(lhead)) != NULL)
        {
        if (save_struct((char *)pal, pal->al_tsize) < 0)
          errct++;

        delete_link(&pal->al_link);

        free(pal);
        }
      }
    }  /* END for (i) */

  /* indicate last of attributes by writing dummy entry */

  memset(&dummy, 0, sizeof(dummy));

  dummy.al_tsize = ENDATTRIBUTES;

  if (save_struct((char *)&dummy, sizeof(dummy)) < 0)
    errct++;

  if (errct != 0)
    {
    return(-1);
    }

  /* SUCCESS */

  return(0);
  }  /* END save_attr() */




#ifndef PBS_MOM
int save_attr_xml(

  struct attribute_def *padef,   /* attribute definition array */
  struct attribute     *pattr,   /* ptr to attribute value array */
  int                   numattr, /* number of attributes in array */
  int                   fds)     /* file descriptor where attributes are written */

  {
  int             i;
  int             rc;
  char            buf[MAXLINE<<8];
  dynamic_string *ds = get_dynamic_string(-1, NULL);

  /* write the opening tag for attributes */
  snprintf(buf,sizeof(buf),"<attributes>\n");
  if ((rc = write_buffer(buf,strlen(buf),fds)) != 0)
    return(rc);

  for (i = 0; i < numattr; i++)
    {
    if (pattr[i].at_flags & ATR_VFLAG_SET)
      {
      buf[0] = '\0';
      clear_dynamic_string(ds);

      if ((rc = attr_to_str(ds, padef+i, pattr[i], TRUE)) != 0)
        {
        if (rc != NO_ATTR_DATA)
          {
          /* ERROR */
          snprintf(log_buffer,sizeof(log_buffer),
            "Not enough space to print attribute %s",
            padef[i].at_name);

          free_dynamic_string(ds);
          return(rc);
          }
        }
      else
        {
        snprintf(buf,sizeof(buf),"<%s>%s</%s>\n",
          padef[i].at_name,
          ds->str,
          padef[i].at_name);

        if ((rc = write_buffer(buf,strlen(buf),fds)) != 0)
          {
          free_dynamic_string(ds);
          return(rc);
          }
        }
      }
    } /* END for each attribute */

  free_dynamic_string(ds);

  /* close the attributes */
  snprintf(buf,sizeof(buf),"</attributes>\n");
  rc = write_buffer(buf,strlen(buf),fds);

  /* we can just return this since its the last write */
  return(rc);
  } /* END save_attr_xml() */
#endif /* ndef PBS_MOM */



/*
 * recov_attr() - read attributes from disk file
 *
 * Recover (reload) attribute from file written by save_attr().
 * Since this is not often done (only on server initialization),
 * Buffering the reads isn't done.
 */

int recov_attr(

  int                   fd,
  void                 *parent,
  struct attribute_def *padef,
  struct attribute     *pattr,
  int                   limit,
  int                   unknown,
  int                   do_actions)

  {
  static char  id[] = "recov_attr";
  int     amt;
  int     i;
  int     index;
  svrattrl *pal = NULL;
  int     palsize = 0;
  svrattrl  tempal;

  /* set all privileges (read and write) for decoding resources */
  /* This is a special (kludge) flag for the recovery case, see */
  /* decode_resc() in lib/Libattr/attr_fn_resc.c                        */

  resc_access_perm = ATR_DFLAG_ACCESS;

  /* For each attribute, read in the attr_extern header */

  while (1)
    {
    i = read(fd, (char *) & tempal, sizeof(tempal));

    if (i != sizeof(tempal))
      {
      log_err(errno, id, "read1");

      return(-1);
      }

    if (tempal.al_tsize == ENDATTRIBUTES)
      break;            /* hit dummy attribute that is eof */

    if (tempal.al_tsize <= (int)sizeof(tempal))
      {
      log_err(-1, id, "attr size too small");

      return(-1);
      }

    /* read in the attribute chunck (name and encoded value) */

    palsize = tempal.al_tsize;

    pal = (svrattrl *)calloc(1, palsize);

    if (pal == NULL)
      {
      log_err(errno, id, "calloc failed");

      return(-1);
      }

    *pal = tempal;

    CLEAR_LINK(pal->al_link);

    /* read in the actual attribute data */

    amt = pal->al_tsize - sizeof(svrattrl);

    i = read(fd, (char *)pal + sizeof(svrattrl), amt);

    if (i != amt)
      {
      log_err(errno, id, "read2");

      free(pal);

      return(-1);
      }

    /* the pointer into the data are of course bad, so reset them */

    pal->al_name = (char *)pal + sizeof(svrattrl);

    if (pal->al_rescln)
      pal->al_resc = pal->al_name + pal->al_nameln;
    else
      pal->al_resc = NULL;

    if (pal->al_valln)
      pal->al_value = pal->al_name + pal->al_nameln + pal->al_rescln;
    else
      pal->al_value = NULL;

    /* find the attribute definition based on the name */

    index = find_attr(padef, pal->al_name, limit);

    if (index < 0)
      {
      /*
       * There are two ways this could happen:
       * 1. if the (job) attribute is in the "unknown" list -
       * keep it there;
       * 2. if the server was rebuilt and an attribute was
       * deleted, -  the fact is logged and the attribute
       * is discarded (system,queue) or kept (job)
       *
       */

      if (unknown > 0)
        {
        index = unknown;
        }
      else
        {
        log_err(-1, id, "unknown attribute discarded");

        free(pal);

        continue;
        }
      }    /* END if (index < 0) */

    (padef + index)->at_decode(
      pattr + index,
      pal->al_name,
      pal->al_resc,
      pal->al_value);

    if ((do_actions) && (padef + index)->at_action != (int (*)())0)
      (padef + index)->at_action(pattr + index, parent, ATR_ACTION_RECOV);

    (pattr + index)->at_flags = pal->al_flags & ~ATR_VFLAG_MODIFY;

    free(pal);
    }  /* END while (1) */

  return(0);
  }  /* END recov_attr() */

/* END attr_recov.c */

