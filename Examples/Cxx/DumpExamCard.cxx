/*=========================================================================

  Program: GDCM (Grassroots DICOM). A DICOM library

  Copyright (c) 2006-2011 Mathieu Malaterre
  All rights reserved.
  See Copyright.txt or http://gdcm.sourceforge.net/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/*

  Try to extract contents of Philips RAW storage class:

(0002,0002) UI [1.2.840.10008.5.1.4.1.1.66]                       # 26,1 Media Storage SOP Class UID
(0002,0003) UI [1.3.46.670589.11.17240.5.23.4.1.3012.2010032409482568018]         # 56,1 Media Storage SOP Instance UID
(0002,0010) UI [1.2.840.10008.1.2.1]                              # 20,1 Transfer Syntax UID
(0002,0012) UI [1.3.46.670589.11.0.0.51.4.4.1]                    # 30,1 Implementation Class UID
(0002,0013) SH [MR DICOM 4.1]                                     # 12,1 Implementation Version Name

 * Everything done in this code is for the sole purpose of writing interoperable
 * software under Sect. 1201 (f) Reverse Engineering exception of the DMCA.
 * If you believe anything in this code violates any law or any of your rights,
 * please contact us (gdcm-developers@lists.sourceforge.net) so that we can
 * find a solution.
 *
 * Everything you do with this code is at your own risk, since decompression
 * algorithm was not written from specification documents.
 *
 * Special thanks to:
 * Triplett,William T for bringing to your attention on this ExamCard stuff
 */
#include "gdcmReader.h"
#include "gdcmDataSet.h"
#include "gdcmPrivateTag.h"

#include <iomanip>

typedef enum
{
  param_float = 0,
  param_integer,
  param_string,
  param_3, // ??
  param_enum,
} param_type;

static const char *gettypenamefromtype( int i)
{
  const char *ret = NULL;
  param_type e = (param_type)i;
  switch( e )
    {
  case param_float:
    ret = "float";
    break;
  case param_integer:
    ret = "int";
    break;
  case param_string:
    ret = "string";
    break;
  case param_3:
    ret = "??";
    break;
  case param_enum:
    ret = "enum";
    break;
    }
  assert( ret );
  return ret;
}

struct header
{
/*
 * TODO:
 * Looks as if we could read all int*, float* and string* at once...
 */
  int32_t v1; // offset to int pointer array ?
  uint16_t nints; // number of ints (max number?)
  uint16_t v3; // always 0 ?
  int32_t v4; // offset to float pointer array ?
  uint32_t nfloats;
  int32_t v6; // offset to string pointer array ?
  uint32_t nstrings;
  int32_t v8; // always 8 ??
  uint32_t numparams;
  uint32_t getnints() const { return nints; }
  uint32_t getnfloats() const { return nfloats; }
  uint32_t getnstrings() const { return nstrings; }
  uint32_t getnparams() const { return numparams; }
  void read( std::istream & is )
    {
    is.read( (char*)&v1,sizeof(v1));
    is.read( (char*)&nints,sizeof(nints));
    is.read( (char*)&v3,sizeof(v3));
    assert( v3 == 0 ); // looks like this is always 0
    is.read( (char*)&v4,sizeof(v4));
    is.read( (char*)&nfloats,sizeof(nfloats));
    is.read( (char*)&v6,sizeof(v6));
    is.read( (char*)&nstrings,sizeof(nstrings));
    is.read( (char*)&v8,sizeof(v8));
    assert( v8 == 8 );
    is.read( (char*)&numparams,sizeof(numparams));
    }
  void print( std::ostream & os )
    {
    os << v1 << ",";
    os << nints << ",";
    os << v3 << ",";
    os << v4 << ",";
    os << nfloats << ",";
    os << v6 << ",";
    os << nstrings << ",";
    os << v8 << ",";
    os << numparams << std::endl;
    }
};

struct param
{
  char name[32+1];
  int8_t boolean;
  int32_t type;
  uint32_t dim;
  uint32_t v4;
  int32_t offset;
  param_type gettype() const { return (param_type)type; }
  uint32_t getdim() const { return dim; }
  void read( std::istream & is )
    {
    is.read( name, 32 + 1);
    //assert( name[32] == 0 ); // fails sometimes...
    // This is always the same issue the string can contains garbarge from previous run,
    // we need to print only until the first \0 character:
    assert( strlen( name ) <= 32 ); // sigh
    is.read( (char*)&boolean,1);
    assert( boolean == 0 || boolean == 1 ); // some kind of bool...
    is.read( (char*)&type, sizeof( type ) );
    assert( gettypenamefromtype( type ) );
    is.read( (char*)&dim, sizeof( dim ) );
    is.read( (char*)&v4, sizeof( v4 ) );
    //assert( v4 == 0 ); // always 0 ? sometimes not...
    const uint32_t cur = is.tellg();
    is.read( (char*)&offset, sizeof( offset ) );
    offset += cur;
    }

  void print( std::ostream & os )
    {
    os << name << ",";
    os << (int)boolean << ",";
    os << type << ",";
    os << dim << ",";
    os << v4 << ",";
    os << offset << std::endl;
    }
  void print2( std::ostream & os, std::istream & is )
    {
    os << std::setw(32) << std::left << name << ",";
    os << std::setw(7) << std::right << gettypenamefromtype(type) << ",";
    os << std::setw(4) << dim << ",";
    os << " ";
    is.seekg( offset );
    switch( type )
      {
    case param_float:
        {
        os.precision(2);
        os << std::fixed;
        for( uint32_t idx = 0; idx < dim; ++idx )
          {
          if( idx ) os << ",";
          float v;
          is.read( (char*)&v, sizeof(v) );
          os << v; // what if the string contains \0 ?
          }
        }
      break;
    case param_integer:
        {
        for( uint32_t idx = 0; idx < dim; ++idx )
          {
          if( idx ) os << ",";
          int32_t v;
          is.read( (char*)&v, sizeof(v) );
          os << v;
          }
        }
      break;
    case param_string:
        {
        std::string v;
        v.resize( dim );
        is.read( &v[0], dim );
        os << v;
        }
      break;
    case param_enum:
        {
        for( uint32_t idx = 0; idx < dim; ++idx )
          {
          if( idx ) os << ",";
          int32_t v;
          is.read( (char*)&v, sizeof(v) );
          os << v;
          }
        }
      break;
      }
    os << ",\n";
    }
};

bool ProcessNested( gdcm::DataSet & ds )
{
  /*
  TODO:
   Looks like the real length of the blob is stored here:
(2005,1132) SQ                                                    # u/l,1 ?
  (fffe,e000) na (Item with undefined length)
    (2005,0011) LO [Philips MR Imaging DD 002 ]                   # 26,1 Private Creator
    (2005,1143) SL 3103                                           # 4,1 ?

Whosit ?
(2005,1132) SQ                                                    # u/l,1 ?
  (fffe,e000) na (Item with undefined length)
    (2005,0011) LO [Philips MR Imaging DD 002 ]                   # 26,1 Private Creator
    (2005,1147) CS [Y ]                                           # 2,1 ?
  */
  bool ret = false;

  //  (2005,1137) PN (LO) [PDF_CONTROL_GEN_PARS]                    # 20,1 ?
  const gdcm::PrivateTag pt0(0x2005,0x37,"Philips MR Imaging DD 002");
  if( !ds.FindDataElement( pt0 ) ) return false;
  const gdcm::DataElement &de0 = ds.GetDataElement( pt0 );
  if( de0.IsEmpty() ) return false;
  const gdcm::ByteValue * bv0 = de0.GetByteValue();
  std::string s0( bv0->GetPointer() , bv0->GetLength() );

  // (2005,1139) LO [IEEE_PDF]                                     # 8,1 ?
  const gdcm::PrivateTag pt1(0x2005,0x39,"Philips MR Imaging DD 002");
  if( !ds.FindDataElement( pt1 ) ) return false;
  const gdcm::DataElement &de1 = ds.GetDataElement( pt1 );
  if( de1.IsEmpty() ) return false;
  const gdcm::ByteValue * bv1 = de1.GetByteValue();
  std::string s1( bv1->GetPointer() , bv1->GetLength() );

  const gdcm::PrivateTag pt(0x2005,0x44,"Philips MR Imaging DD 002");
  if( !ds.FindDataElement( pt ) ) return false;
  const gdcm::DataElement &de = ds.GetDataElement( pt );
  if( de.IsEmpty() ) return false;
  const gdcm::ByteValue * bv = de.GetByteValue();

  if( s1 == "IEEE_PDF" )
    {
    //  std::cout << "Len= " << bv->GetLength() << std::endl;
#if 0
    std::string fn = gdcm::LOComp::Trim( s.c_str() ); // remove trailing slash
    std::ofstream out( fn.c_str() );
    out.write( bv->GetPointer(), bv->GetLength() );
    out.close();
#endif

    std::istringstream is;
    std::string dup( bv->GetPointer(), bv->GetLength() );
    is.str( dup );

    header h;
    h.read( is );
#if 0
    std::cout << s0.c_str() << std::endl;
    h.print( std::cout );
#endif

    assert( is.tellg() == 0x20 );
    is.seekg( 0x20 );

    std::vector< param > params;
    param p;
    for( uint32_t i = 0; i < h.getnparams(); ++i )
      {
      p.read( is );
      //p.print( std::cout );
      params.push_back( p );
      }
    std::string fn = gdcm::LOComp::Trim( s0.c_str() ); // remove trailing slash
    fn += ".csv";
    std::ofstream csv( fn.c_str() );

    // let's do some bookeeping:
    uint32_t nfloats = 0;
    uint32_t nints = 0;
    uint32_t nstrings = 0;
    for( std::vector<param>::const_iterator it = params.begin();
      it != params.end(); ++it )
      {
      param_type type = it->gettype();
      switch( type )
        {
      case param_float:
        nfloats += it->getdim();
        break;
      case param_integer:
        nints += it->getdim();
        break;
      case param_string:
        nstrings += it->getdim();
        break;
      default:
        ;
        }
      }
#if 0
    std::cout << "Stats:" << std::endl;
    std::cout << "nfloats:" << nfloats << std::endl;
    std::cout << "nints:" << nints << std::endl;
    std::cout << "nstrings:" << nstrings << std::endl;
#endif
    assert( h.getnints() >= nints );
    assert( h.getnfloats() >= nfloats );
    assert( h.getnstrings() >= nstrings);

    for( uint32_t i = 0; i < h.getnparams(); ++i )
      {
      params[i].print2( csv, is );
      }
    csv.close();
    ret = true;
    }
  else if( s1 == "BINARY" )
    {
    std::cerr << "BINARY is not handled" << std::endl;
    std::string fn = gdcm::LOComp::Trim( s0.c_str() ); // remove trailing slash
    fn += ".bin";
    std::ofstream out( fn.c_str() );
    //out.write( bv->GetPointer() + 512, bv->GetLength() - 512);
    out.write( bv->GetPointer() , bv->GetLength() );
    out.close();

    ret = true;
    }
  // else -> ret == false

  return ret;
}

int main(int argc, char *argv[])
{
  if( argc < 2 ) return 1;
  const char *filename = argv[1];
  gdcm::Reader reader;
  reader.SetFileName( filename );
  if( !reader.Read() )
    {
    std::cerr << "Failed to read: " << filename << std::endl;
    return 1;
    }
  const gdcm::DataSet& ds = reader.GetFile().GetDataSet();
/*
(2005,1132) SQ                                                    # u/l,1 ?
  (fffe,e000) na (Item with undefined length)
    (2005,0011) LO [Philips MR Imaging DD 002 ]                   # 26,1 Private Creator
    (2005,1137) PN (LO) [PDF_CONTROL_GEN_PARS]                    # 20,1 ?
    (2005,1138) PN (LO) (no value)                                # 0,1 ?
    (2005,1139) PN (LO) [IEEE_PDF]                                # 8,1 ?
    (2005,1140) PN (LO) (no value)                                # 0,1 ?
    (2005,1141) PN (LO) (no value)                                # 0,1 ?
    (2005,1143) SL 3103                                           # 4,1 ?
    (2005,1144) OW 66\05\00\00\3b\01\00\00\4a\0a\00\00\0e\00\00\00\7a\0a\00\00\95\01\00\00\08\00\00\00\1b\00\00\00\43\47\45\4e\5f\75\73\65\72\5f\64\65\66\5f\6b\65\79\5f\6e\61\6d\65\73\00\00\00\00\00\00\00\00\00         # 3104,1 ?
    (2005,1147) CS [Y ]                                           # 2,1 ?
  (fffe,e00d)
*/
  const gdcm::PrivateTag pt(0x2005,0x32,"Philips MR Imaging DD 002");
  if( !ds.FindDataElement( pt ) ) return 1;
  const gdcm::DataElement &de = ds.GetDataElement( pt );
  if( de.IsEmpty() ) return 1;

  gdcm::SequenceOfItems *sqi = de.GetValueAsSQ();
  if ( !sqi ) return 1;
  gdcm::SequenceOfItems::SizeType s = sqi->GetNumberOfItems();
  for( gdcm::SequenceOfItems::SizeType i = 1; i <= s; ++i )
    {
    gdcm::Item &item = sqi->GetItem(i);

    gdcm::DataSet &nestedds = item.GetNestedDataSet();

    if( !ProcessNested( nestedds ) ) return 1;
    }

  return 0;
}
