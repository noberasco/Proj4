/******************************************************************************
 * $Id: pj_init.c 2163 2012-02-21 01:53:19Z warmerdam $
 *
 * Project:  PROJ.4
 * Purpose:  Initialize projection object from string definition.  Includes
 *           pj_init(), pj_init_plus() and pj_free() function.
 * Author:   Gerald Evenden, Frank Warmerdam <warmerdam@pobox.com>
 *
 ******************************************************************************
 * Copyright (c) 1995, Gerald Evenden
 * Copyright (c) 2002, Frank Warmerdam <warmerdam@pobox.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#define PJ_LIB__
#include <projects.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <locale.h>

PJ_CVSID("$Id: pj_init.c 2163 2012-02-21 01:53:19Z warmerdam $");

/************************************************************************/
/*                              get_opt()                               */
/************************************************************************/
static paralist *
get_opt(projCtx ctx, paralist **start, FILE *fid, char *name, paralist *next) {
    char sword[302], *word = sword+1;
    int first = 1, len, c;

    len = (int)strlen(name);
    *sword = 't';
    while (fscanf(fid, "%300s", word) == 1) {
        if (*word == '#') /* skip comments */
            while((c = fgetc(fid)) != EOF && c != '\n') ;
        else if (*word == '<') { /* control name */
            if (first && !strncmp(name, word + 1, len)
                && word[len + 1] == '>')
                first = 0;
            else if (!first && *word == '<') {
                while((c = fgetc(fid)) != EOF && c != '\n') ;
                break;
            }
        } else if (!first && !pj_param(ctx, *start, sword).i) {
            /* don't default ellipse if datum, ellps or any earth model
               information is set. */
            if( strncmp(word,"ellps=",6) != 0 
                || (!pj_param(ctx, *start, "tdatum").i 
                    && !pj_param(ctx, *start, "tellps").i 
                    && !pj_param(ctx, *start, "ta").i 
                    && !pj_param(ctx, *start, "tb").i 
                    && !pj_param(ctx, *start, "trf").i 
                    && !pj_param(ctx, *start, "tf").i) )
            {
                next = next->next = pj_mkparam(word);
            }
        }
    }

    if (errno == 25)
        errno = 0;
    return next;
}

/************************************************************************/
/*                            get_defaults()                            */
/************************************************************************/
static paralist *
get_defaults(projCtx ctx, paralist **start, paralist *next, char *name) {
    FILE *fid;

    if ( (fid = pj_open_lib(ctx,"proj_def.dat", "rt")) != NULL) {
        next = get_opt(ctx, start, fid, "general", next);
        rewind(fid);
        next = get_opt(ctx, start, fid, name, next);
        (void)fclose(fid);
    }
    if (errno)
        errno = 0; /* don't care if can't open file */
    ctx->last_errno = 0;
    
    return next;
}

/************************************************************************/
/*                              get_init()                              */
/************************************************************************/
static paralist *
get_init(projCtx ctx, paralist **start, paralist *next, char *name) {
    char fname[MAX_PATH_FILENAME+ID_TAG_MAX+3], *opt;
    FILE *fid;
    paralist *init_items = NULL;
    const paralist *orig_next = next;

    (void)strncpy(fname, name, MAX_PATH_FILENAME + ID_TAG_MAX + 1);
	
    /* 
    ** Search for file/key pair in cache 
    */
	
    init_items = pj_search_initcache( name );
    if( init_items != NULL )
    {
        next->next = init_items;
        while( next->next != NULL )
            next = next->next;
        return next;
    }

    /*
    ** Otherwise we try to open the file and search for it.
    */
    if ((opt = strrchr(fname, ':')) != NULL)
        *opt++ = '\0';
    else { pj_ctx_set_errno(ctx,-3); return NULL; }

    if ( (fid = pj_open_lib(ctx,fname, "rt")) != NULL)
        next = get_opt(ctx, start, fid, opt, next);
    else
        return NULL;
    (void)fclose(fid);
    if (errno == 25)
        errno = 0; /* unknown problem with some sys errno<-25 */

    /* 
    ** If we seem to have gotten a result, insert it into the 
    ** init file cache.
    */
    if( next != NULL && next != orig_next )
        pj_insert_initcache( name, orig_next->next );

    return next;
}

/************************************************************************/
/*                            pj_init_plus()                            */
/*                                                                      */
/*      Same as pj_init() except it takes one argument string with      */
/*      individual arguments preceeded by '+', such as "+proj=utm       */
/*      +zone=11 +ellps=WGS84".                                         */
/************************************************************************/

PJ *
pj_init_plus( const char *definition )

{
    return pj_init_plus_ctx( pj_get_default_ctx(), definition );
}

PJ *
pj_init_plus_ctx( projCtx ctx, const char *definition )
{
#define MAX_ARG 200
    char	*argv[MAX_ARG];
    char	*defn_copy;
    int		argc = 0, i, blank_count = 0;
    PJ	    *result;
    
    /* make a copy that we can manipulate */
    defn_copy = (char *) pj_malloc( strlen(definition)+1 );
    strcpy( defn_copy, definition );

    /* split into arguments based on '+' and trim white space */

    for( i = 0; defn_copy[i] != '\0'; i++ )
    {
        switch( defn_copy[i] )
        {
          case '+':
            if( i == 0 || defn_copy[i-1] == '\0' || blank_count > 0 )
            {
                /* trim trailing spaces from the previous param */
                if( blank_count > 0 )
                {
                    defn_copy[i - blank_count] = '\0';
                    blank_count = 0;
                }
                
                if( argc+1 == MAX_ARG )
                {
                    pj_ctx_set_errno( ctx, -44 );
                    return NULL;
                }
                
                argv[argc++] = defn_copy + i + 1;
            }
            break;

          case ' ':
          case '\t':
          case '\n':
            /* trim leading spaces from the current param */
            if( i == 0 || defn_copy[i-1] == '\0' || argc == 0 || argv[argc-1] == defn_copy + i )
                defn_copy[i] = '\0';
            else
                blank_count++;
            break;

          default:
            /* reset blank_count */
            blank_count = 0;
        }
    }
    /* trim trailing spaces from the last param */
    defn_copy[i - blank_count] = '\0';

    /* perform actual initialization */
    result = pj_init_ctx( ctx, argc, argv );

    pj_dalloc( defn_copy );

    return result;
}

/************************************************************************/
/*                              pj_init()                               */
/*                                                                      */
/*      Main entry point for initialing a PJ projections                */
/*      definition.  Note that the projection specific function is      */
/*      called to do the initial allocation so it can be created        */
/*      large enough to hold projection specific parameters.            */
/************************************************************************/

PJ *
pj_init(int argc, char **argv) {
    return pj_init_ctx( pj_get_default_ctx(), argc, argv );
}

PJ *
pj_init_ctx(projCtx ctx, int argc, char **argv) {
    char *s, *name;
    paralist *start = NULL;
    PJ *(*proj)(PJ *);
    paralist *curr;
    int i;
    PJ *PIN = 0;
    char *old_locale;

    ctx->last_errno = 0;
    start = NULL;

    old_locale = strdup(setlocale(LC_NUMERIC, NULL));
    if( strcmp(old_locale,"C") != 0 )
        setlocale(LC_NUMERIC,"C");

    /* put arguments into internal linked list */
    if (argc <= 0) { pj_ctx_set_errno( ctx, -1 ); goto bum_call; }
    for (i = 0; i < argc; ++i)
        if (i)
            curr = curr->next = pj_mkparam(argv[i]);
        else
            start = curr = pj_mkparam(argv[i]);
    if (ctx->last_errno) goto bum_call;

    /* check if +init present */
    if (pj_param(ctx, start, "tinit").i) {
        paralist *last = curr;

        if (!(curr = get_init(ctx,&start, curr, pj_param(ctx, start, "sinit").s)))
            goto bum_call;
        if (curr == last) { pj_ctx_set_errno( ctx, -2); goto bum_call; }
    }

    /* find projection selection */
    if (!(name = pj_param(ctx, start, "sproj").s))
    { pj_ctx_set_errno( ctx, -4 ); goto bum_call; }
    for (i = 0; (s = pj_list[i].id) && strcmp(name, s) ; ++i) ;
    if (!s) { pj_ctx_set_errno( ctx, -5 ); goto bum_call; }

    /* set defaults, unless inhibited */
    if (!pj_param(ctx, start, "bno_defs").i)
        curr = get_defaults(ctx,&start, curr, name);
    proj = (PJ *(*)(PJ *)) pj_list[i].proj;

    /* allocate projection structure */
    if (!(PIN = (*proj)(0))) goto bum_call;
    PIN->ctx = ctx;
    PIN->params = start;
    PIN->is_latlong = 0;
    PIN->is_geocent = 0;
    PIN->is_long_wrap_set = 0;
    PIN->long_wrap_center = 0.0;
    strcpy( PIN->axis, "enu" );

    PIN->gridlist = NULL;
    PIN->gridlist_count = 0;

    PIN->vgridlist_geoid = NULL;
    PIN->vgridlist_geoid_count = 0;

    /* set datum parameters */
    if (pj_datum_set(ctx, start, PIN)) goto bum_call;

    /* set ellipsoid/sphere parameters */
    if (pj_ell_set(ctx, start, &PIN->a, &PIN->es)) goto bum_call;

    PIN->a_orig = PIN->a;
    PIN->es_orig = PIN->es;

    PIN->e = sqrt(PIN->es);
    PIN->ra = 1. / PIN->a;
    PIN->one_es = 1. - PIN->es;
    if (PIN->one_es == 0.) { pj_ctx_set_errno( ctx, -6 ); goto bum_call; }
    PIN->rone_es = 1./PIN->one_es;

    /* Now that we have ellipse information check for WGS84 datum */
    if( PIN->datum_type == PJD_3PARAM 
        && PIN->datum_params[0] == 0.0
        && PIN->datum_params[1] == 0.0
        && PIN->datum_params[2] == 0.0
        && PIN->a == 6378137.0
        && ABS(PIN->es - 0.006694379990) < 0.000000000050 )/*WGS84/GRS80*/
    {
        PIN->datum_type = PJD_WGS84;
    }
        
    /* set PIN->geoc coordinate system */
    PIN->geoc = (PIN->es && pj_param(ctx, start, "bgeoc").i);

    /* over-ranging flag */
    PIN->over = pj_param(ctx, start, "bover").i;

    /* vertical datum geoid grids */
    PIN->has_geoid_vgrids = pj_param(ctx, start, "tgeoidgrids").i;
    if( PIN->has_geoid_vgrids ) /* we need to mark it as used. */
        pj_param(ctx, start, "sgeoidgrids");

    /* longitude center for wrapping */
    PIN->is_long_wrap_set = pj_param(ctx, start, "tlon_wrap").i;
    if (PIN->is_long_wrap_set)
        PIN->long_wrap_center = pj_param(ctx, start, "rlon_wrap").f;

    /* axis orientation */
    if( (pj_param(ctx, start,"saxis").s) != NULL )
    {
        static const char *axis_legal = "ewnsud";
        const char *axis_arg = pj_param(ctx, start,"saxis").s;
        if( strlen(axis_arg) != 3 )
        {
            pj_ctx_set_errno( ctx, PJD_ERR_AXIS );
            goto bum_call;
        }

        if( strchr( axis_legal, axis_arg[0] ) == NULL
            || strchr( axis_legal, axis_arg[1] ) == NULL
            || strchr( axis_legal, axis_arg[2] ) == NULL)
        {
            pj_ctx_set_errno( ctx, PJD_ERR_AXIS );
            goto bum_call;
        }

        /* it would be nice to validate we don't have on axis repeated */
        strcpy( PIN->axis, axis_arg );
    }

    PIN->is_long_wrap_set = pj_param(ctx, start, "tlon_wrap").i;
    if (PIN->is_long_wrap_set)
        PIN->long_wrap_center = pj_param(ctx, start, "rlon_wrap").f;

    /* central meridian */
    PIN->lam0=pj_param(ctx, start, "rlon_0").f;

    /* central latitude */
    PIN->phi0 = pj_param(ctx, start, "rlat_0").f;

    /* false easting and northing */
    PIN->x0 = pj_param(ctx, start, "dx_0").f;
    PIN->y0 = pj_param(ctx, start, "dy_0").f;

    /* general scaling factor */
    if (pj_param(ctx, start, "tk_0").i)
        PIN->k0 = pj_param(ctx, start, "dk_0").f;
    else if (pj_param(ctx, start, "tk").i)
        PIN->k0 = pj_param(ctx, start, "dk").f;
    else
        PIN->k0 = 1.;
    if (PIN->k0 <= 0.) {
        pj_ctx_set_errno( ctx, -31 );
        goto bum_call;
    }

    /* set units */
    s = 0;
    if ((name = pj_param(ctx, start, "sunits").s) != NULL) { 
        for (i = 0; (s = pj_units[i].id) && strcmp(name, s) ; ++i) ;
        if (!s) { pj_ctx_set_errno( ctx, -7 ); goto bum_call; }
        s = pj_units[i].to_meter;
    }
    if (s || (s = pj_param(ctx, start, "sto_meter").s)) {
        PIN->to_meter = strtod(s, &s);
        if (*s == '/') /* ratio number */
            PIN->to_meter /= strtod(++s, 0);
        PIN->fr_meter = 1. / PIN->to_meter;
    } else
        PIN->to_meter = PIN->fr_meter = 1.;

    /* set vertical units */
    s = 0;
    if ((name = pj_param(ctx, start, "svunits").s) != NULL) { 
        for (i = 0; (s = pj_units[i].id) && strcmp(name, s) ; ++i) ;
        if (!s) { pj_ctx_set_errno( ctx, -7 ); goto bum_call; }
        s = pj_units[i].to_meter;
    }
    if (s || (s = pj_param(ctx, start, "svto_meter").s)) {
        PIN->vto_meter = strtod(s, &s);
        if (*s == '/') /* ratio number */
            PIN->vto_meter /= strtod(++s, 0);
        PIN->vfr_meter = 1. / PIN->vto_meter;
    } else {
        PIN->vto_meter = PIN->to_meter;
        PIN->vfr_meter = PIN->fr_meter;
    }

    /* prime meridian */
    s = 0;
    if ((name = pj_param(ctx, start, "spm").s) != NULL) { 
        const char *value = NULL;
        char *next_str = NULL;

        for (i = 0; pj_prime_meridians[i].id != NULL; ++i )
        {
            if( strcmp(name,pj_prime_meridians[i].id) == 0 )
            {
                value = pj_prime_meridians[i].defn;
                break;
            }
        }
            
        if( value == NULL 
            && (dmstor_ctx(ctx,name,&next_str) != 0.0  || *name == '0')
            && *next_str == '\0' )
            value = name;

        if (!value) { pj_ctx_set_errno( ctx, -46 ); goto bum_call; }
        PIN->from_greenwich = dmstor_ctx(ctx,value,NULL);
    }
    else
        PIN->from_greenwich = 0.0;

    /* projection specific initialization */
    if (!(PIN = (*proj)(PIN)) || ctx->last_errno) {
      bum_call: /* cleanup error return */
        if (PIN)
            pj_free(PIN);
        else
            for ( ; start; start = curr) {
                curr = start->next;
                pj_dalloc(start);
            }
        PIN = 0;
    }

    if( strcmp(old_locale,"C") != 0 )
        setlocale(LC_NUMERIC,old_locale);
    free( (char*)old_locale );

    return PIN;
}

/************************************************************************/
/*                              pj_free()                               */
/*                                                                      */
/*      This is the application callable entry point for destroying     */
/*      a projection definition.  It does work generic to all           */
/*      projection types, and then calls the projection specific        */
/*      free function (P->pfree()) to do local work.  This maps to      */
/*      the FREEUP code in the individual projection source files.      */
/************************************************************************/

void
pj_free(PJ *P) {
    if (P) {
        paralist *t = P->params, *n;

        /* free parameter list elements */
        for (t = P->params; t; t = n) {
            n = t->next;
            pj_dalloc(t);
        }

        /* free array of grid pointers if we have one */
        if( P->gridlist != NULL )
            pj_dalloc( P->gridlist );
        
        /* free projection parameters */
        P->pfree(P);
    }
}


