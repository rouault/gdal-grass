/******************************************************************************
 * $Id$
 *
 * Project:  GRASS Driver
 * Purpose:  Implement GRASS raster read/write support
 * Author:   Frank Warmerdam, warmerda@home.com
 *
 ******************************************************************************
 * Copyright (c) 2000, Frank Warmerdam
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
 ******************************************************************************
 *
 * $Log$
 * Revision 1.1  2000/09/11 13:31:52  warmerda
 * New
 *
 */


#include "gdal_priv.h"
#include "cpl_string.h"

CPL_C_START
#include <libgrass.h>
CPL_C_END

static GDALDriver	*poGRASSDriver = NULL;

CPL_C_START
void	GDALRegister_GRASS(void);
CPL_C_END

/************************************************************************/
/* ==================================================================== */
/*				GRASSDataset				*/
/* ==================================================================== */
/************************************************************************/

class GRASSRasterBand;

class GRASSDataset : public GDALDataset
{
    friend	GRASSRasterBand;

  public:
                 GRASSDataset();
                 ~GRASSDataset();

    static GDALDataset *Open( GDALOpenInfo * );
};

/************************************************************************/
/* ==================================================================== */
/*                            GRASSRasterBand                           */
/* ==================================================================== */
/************************************************************************/

class GRASSRasterBand : public GDALRasterBand
{
    friend	GRASSDataset;

    int		hCell;
    int         nGRSType;

  public:

                   GRASSRasterBand( GRASSDataset *, int, 
                                    const char *, const char * );

    virtual CPLErr IReadBlock( int, int, void * );
};


/************************************************************************/
/*                          GRASSRasterBand()                           */
/************************************************************************/

GRASSRasterBand::GRASSRasterBand( GRASSDataset *poDS, int nBand,
                                  const char * pszMapset,
                                  const char * pszCellName )

{
    struct Cell_head	sCellInfo;

    this->poDS = poDS;
    this->nBand = nBand;

    G_get_cellhd( (char *) pszCellName, (char *) pszMapset, &sCellInfo );
    nGRSType = G_raster_map_type( (char *) pszCellName, (char *) pszMapset );

    if( nGRSType == CELL_TYPE && sCellInfo.format == 0 )
        this->eDataType = GDT_Byte;
    else if( nGRSType == CELL_TYPE && sCellInfo.format == 1 )
        this->eDataType = GDT_UInt16;
    else if( nGRSType == CELL_TYPE )
        this->eDataType = GDT_UInt32;
    else if( nGRSType == FCELL_TYPE )
        this->eDataType = GDT_Float32;
    else if( nGRSType == DCELL_TYPE )
        this->eDataType = GDT_Float64;
    
    nBlockXSize = poDS->nRasterXSize;;
    nBlockYSize = 1;

    hCell = G_open_cell_old((char *) pszCellName, (char *) pszMapset);
}

/************************************************************************/
/*                             IReadBlock()                             */
/************************************************************************/

CPLErr GRASSRasterBand::IReadBlock( int nBlockXOff, int nBlockYOff,
                                  void * pImage )

{
    if( eDataType == GDT_Float32 || eDataType == GDT_Float64 
        || eDataType == GDT_UInt32 )
        G_get_raster_row( hCell, pImage, nBlockYOff, nGRSType  );
    else
    {
        GUInt32 *panRow = (GUInt32 *) CPLMalloc(4 * nBlockXSize);
        
        G_get_raster_row( hCell, panRow, nBlockYOff, nGRSType  );
        GDALCopyWords( panRow, GDT_UInt32, 4, 
                       pImage, eDataType, GDALGetDataTypeSize(eDataType)/8,
                       nBlockXSize );

        CPLFree( panRow );
    }

    return CE_None;
}

/************************************************************************/
/* ==================================================================== */
/*                             GRASSDataset                             */
/* ==================================================================== */
/************************************************************************/


/************************************************************************/
/*                            GRASSDataset()                            */
/************************************************************************/

GRASSDataset::GRASSDataset()

{
}

/************************************************************************/
/*                           ~GRASSDataset()                            */
/************************************************************************/

GRASSDataset::~GRASSDataset()

{
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *GRASSDataset::Open( GDALOpenInfo * poOpenInfo )

{
/* -------------------------------------------------------------------- */
/*      Are we dealing with a direct path to a GRASS cellhd file?       */
/*                                                                      */
/*      Eventually we will come up with other methods as well.          */
/* -------------------------------------------------------------------- */
    if( poOpenInfo->nHeaderBytes > 100 
        && EQUALN((const char *) poOpenInfo->pabyHeader,"proj:",5) )
    {
    }

    else
        return NULL;

/* -------------------------------------------------------------------- */
/*      Can we establish a database, location and mapset?               */
/* -------------------------------------------------------------------- */
    char 	*pszCell, *pszMapset, *pszLocation, *pszGISDB;
    char  	*pszMapsetPath, *pszLocationPath, *pszCellhdPath;

    pszCell = CPLStrdup(CPLGetFilename(poOpenInfo->pszFilename ));
    pszCellhdPath = CPLStrdup(CPLGetPath(poOpenInfo->pszFilename));
    pszMapsetPath = CPLStrdup(CPLGetPath(pszCellhdPath));
    CPLFree( pszCellhdPath );
    pszMapset = CPLStrdup(CPLGetFilename(pszMapsetPath));
    pszLocationPath = CPLStrdup(CPLGetPath(pszMapsetPath));
    CPLFree( pszMapsetPath );
    pszLocation = CPLStrdup(CPLGetFilename(pszLocationPath));
    pszGISDB = CPLStrdup(CPLGetPath(pszLocationPath));
    CPLFree( pszLocationPath );

/* -------------------------------------------------------------------- */
/*      Try to initialize with the selected db, location and mapset.    */
/* -------------------------------------------------------------------- */
    int nInitError;

    nInitError = G_gisinit_2( "GDAL", pszGISDB, pszLocation, pszMapset );
    if( nInitError != 0 )
    {
        /* notdef: we should likely report an error through CPL */
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Create a corresponding GDALDataset.                             */
/* -------------------------------------------------------------------- */
    GRASSDataset 	*poDS;

    poDS = new GRASSDataset();

    /* notdef: should only allow read access to an existing cell, right? */
    poDS->eAccess = poOpenInfo->eAccess;
    
/* -------------------------------------------------------------------- */
/*      Capture some information from the file that is of interest.     */
/* -------------------------------------------------------------------- */
    struct Cell_head	sCellInfo;
    
    if( G_get_cellhd( pszCell, pszMapset, &sCellInfo ) != 0 )
    {
        /* notdef: report failure. */
        return NULL;
    }

    poDS->nRasterXSize = sCellInfo.cols;
    poDS->nRasterYSize = sCellInfo.rows;

    G_set_window( &sCellInfo );

/* -------------------------------------------------------------------- */
/*      Create band information objects.                                */
/* -------------------------------------------------------------------- */
    poDS->SetBand( 1, new GRASSRasterBand( poDS, 1, pszMapset, pszCell ) );

    return poDS;
}

/************************************************************************/
/*                          GDALRegister_GRASS()                        */
/************************************************************************/

void GDALRegister_GRASS()

{
    GDALDriver	*poDriver;

    if( poGRASSDriver == NULL )
    {
        poGRASSDriver = poDriver = new GDALDriver();
        
        poDriver->pszShortName = "GRASS";
        poDriver->pszLongName = "GRASS Database Rasters";
#ifdef notdef
        poDriver->pszHelpTopic = "frmt_various.html#GRASS";
#endif
        
        poDriver->pfnOpen = GRASSDataset::Open;

#ifdef notdef
        poDriver->pfnCreateCopy = GRASSCreateCopy;
#endif

        GetGDALDriverManager()->RegisterDriver( poDriver );
    }
}

