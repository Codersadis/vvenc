/* -----------------------------------------------------------------------------
Software Copyright License for the Fraunhofer Software Library VVenc

(c) Copyright (2019-2020) Fraunhofer-Gesellschaft zur Förderung der angewandten Forschung e.V. 

1.    INTRODUCTION

The Fraunhofer Software Library VVenc (“Fraunhofer Versatile Video Encoding Library”) is software that implements (parts of) the Versatile Video Coding Standard - ITU-T H.266 | MPEG-I - Part 3 (ISO/IEC 23090-3) and related technology. 
The standard contains Fraunhofer patents as well as third-party patents. Patent licenses from third party standard patent right holders may be required for using the Fraunhofer Versatile Video Encoding Library. It is in your responsibility to obtain those if necessary. 

The Fraunhofer Versatile Video Encoding Library which mean any source code provided by Fraunhofer are made available under this software copyright license. 
It is based on the official ITU/ISO/IEC VVC Test Model (VTM) reference software whose copyright holders are indicated in the copyright notices of its source files. The VVC Test Model (VTM) reference software is licensed under the 3-Clause BSD License and therefore not subject of this software copyright license.

2.    COPYRIGHT LICENSE

Internal use of the Fraunhofer Versatile Video Encoding Library, in source and binary forms, with or without modification, is permitted without payment of copyright license fees for non-commercial purposes of evaluation, testing and academic research. 

No right or license, express or implied, is granted to any part of the Fraunhofer Versatile Video Encoding Library except and solely to the extent as expressly set forth herein. Any commercial use or exploitation of the Fraunhofer Versatile Video Encoding Library and/or any modifications thereto under this license are prohibited.

For any other use of the Fraunhofer Versatile Video Encoding Library than permitted by this software copyright license You need another license from Fraunhofer. In such case please contact Fraunhofer under the CONTACT INFORMATION below.

3.    LIMITED PATENT LICENSE

As mentioned under 1. Fraunhofer patents are implemented by the Fraunhofer Versatile Video Encoding Library. If You use the Fraunhofer Versatile Video Encoding Library in Germany, the use of those Fraunhofer patents for purposes of testing, evaluating and research and development is permitted within the statutory limitations of German patent law. However, if You use the Fraunhofer Versatile Video Encoding Library in a country where the use for research and development purposes is not permitted without a license, you must obtain an appropriate license from Fraunhofer. It is Your responsibility to check the legal requirements for any use of applicable patents.    

Fraunhofer provides no warranty of patent non-infringement with respect to the Fraunhofer Versatile Video Encoding Library.


4.    DISCLAIMER

The Fraunhofer Versatile Video Encoding Library is provided by Fraunhofer "AS IS" and WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, including but not limited to the implied warranties fitness for a particular purpose. IN NO EVENT SHALL FRAUNHOFER BE LIABLE for any direct, indirect, incidental, special, exemplary, or consequential damages, including but not limited to procurement of substitute goods or services; loss of use, data, or profits, or business interruption, however caused and on any theory of liability, whether in contract, strict liability, or tort (including negligence), arising in any way out of the use of the Fraunhofer Versatile Video Encoding Library, even if advised of the possibility of such damage.

5.    CONTACT INFORMATION

Fraunhofer Heinrich Hertz Institute
Attention: Video Coding & Analytics Department
Einsteinufer 37
10587 Berlin, Germany
www.hhi.fraunhofer.de/vvc
vvc@hhi.fraunhofer.de
----------------------------------------------------------------------------- */


/** \file     UnitTool.cpp
 *  \brief    defines operations for basic units
 */

#include "UnitTools.h"
#include "Unit.h"
#include "Slice.h"
#include "Picture.h"
#include "dtrace_next.h"

#include <utility>
#include <algorithm>

//! \ingroup CommonLib
//! \{

namespace vvenc {

// CS tools

void clipColPos(int& posX, int& posY, const PredictionUnit& pu);


bool CS::isDualITree( const CodingStructure &cs )
{
  return cs.slice->isIntra() && !cs.pcv->ISingleTree;
}

UnitArea CS::getArea( const CodingStructure &cs, const UnitArea& area, const ChannelType chType, const TreeType treeType )
{
  return isDualITree( cs ) || treeType != TREE_D ? area.singleChan( chType ) : area;
}

void CS::setRefinedMotionField(CodingStructure &cs)
{
  MotionBuf   mb     = cs.getMotionBuf();
  MotionInfo* orgPtr = mb.buf;
  
  for( CodingUnit *cu : cs.cus )
  {
    PredictionUnit &pu = *cu->pu;
    
    if( isLuma( pu.chType ) && PU::checkDMVRCondition( pu ) )
    {
      const int dy = std::min<int>( pu.lumaSize().height, DMVR_SUBCU_SIZE );
      const int dx = std::min<int>( pu.lumaSize().width,  DMVR_SUBCU_SIZE );
      
      static const unsigned scale = 4 * std::max<int>(1, 4 * AMVP_DECIMATION_FACTOR / 4);
      static const unsigned mask  = scale - 1;

      const Position puPos = pu.lumaPos();
      const Mv mv0 = pu.mv[0];
      const Mv mv1 = pu.mv[1];

      for( int y = puPos.y, num = 0; y < ( puPos.y + pu.lumaSize().height ); y = y + dy )
      {
        for( int x = puPos.x; x < ( puPos.x + pu.lumaSize().width ); x = x + dx, num++ )
        {
          const Mv subPuMv0 = mv0 + pu.mvdL0SubPu[num];
          const Mv subPuMv1 = mv1 - pu.mvdL0SubPu[num];

          int y2 = ( ( y - 1 ) & ~mask ) + scale;

          for( ; y2 < y + dy; y2 += scale )
          {
            int x2 = ( ( x - 1 ) & ~mask ) + scale;

            for( ; x2 < x + dx; x2 += scale )
            {
              const Position mbPos = g_miScaling.scale( Position{ x2, y2 } );
              mb.buf = orgPtr + rsAddr( mbPos, mb.stride );

              MotionInfo& mi = *mb.buf;

              mi.mv[0] = subPuMv0;
              mi.mv[1] = subPuMv1;
            }
          }
        }
      }
    }
  }
}
// CU tools

bool CU::getRprScaling( const SPS* sps, const PPS* curPPS, Picture* refPic, int& xScale, int& yScale )
{
  const Window& curScalingWindow = curPPS->scalingWindow;
  int curPicWidth = curPPS->picWidthInLumaSamples   - SPS::getWinUnitX( sps->chromaFormatIdc ) * (curScalingWindow.winLeftOffset + curScalingWindow.winRightOffset);
  int curPicHeight = curPPS->picHeightInLumaSamples - SPS::getWinUnitY( sps->chromaFormatIdc ) * (curScalingWindow.winTopOffset  + curScalingWindow.winBottomOffset);

  const Window& refScalingWindow = refPic->cs->pps->scalingWindow;
  int refPicWidth = refPic->cs->pps->picWidthInLumaSamples   - SPS::getWinUnitX( sps->chromaFormatIdc ) * (refScalingWindow.winLeftOffset + refScalingWindow.winRightOffset);
  int refPicHeight = refPic->cs->pps->picHeightInLumaSamples - SPS::getWinUnitY( sps->chromaFormatIdc) * (refScalingWindow.winTopOffset  + refScalingWindow.winBottomOffset);

  xScale = ( ( refPicWidth << SCALE_RATIO_BITS ) + ( curPicWidth >> 1 ) ) / curPicWidth;
  yScale = ( ( refPicHeight << SCALE_RATIO_BITS ) + ( curPicHeight >> 1 ) ) / curPicHeight;

  int curSeqMaxPicWidthY = sps->maxPicWidthInLumaSamples;                  // pic_width_max_in_luma_samples
  int curSeqMaxPicHeightY = sps->maxPicHeightInLumaSamples;                // pic_height_max_in_luma_samples
  int curPicWidthY = curPPS->picWidthInLumaSamples;                        // pic_width_in_luma_samples
  int curPicHeightY = curPPS->picHeightInLumaSamples;                      // pic_height_in_luma_samples 
  int max8MinCbSizeY = std::max((int)8, (1<<sps->log2MinCodingBlockSize)); // Max(8, MinCbSizeY)

  CHECK((curPicWidth * curSeqMaxPicWidthY) < refPicWidth * (curPicWidthY - max8MinCbSizeY), "(curPicWidth * curSeqMaxPicWidthY) should be greater than or equal to refPicWidth * (curPicWidthY - max8MinCbSizeY))");
  CHECK((curPicHeight * curSeqMaxPicHeightY) < refPicHeight * (curPicHeightY - max8MinCbSizeY), "(curPicHeight * curSeqMaxPicHeightY) should be greater than or equal to refPicHeight * (curPicHeightY - max8MinCbSizeY))");

  return refPic->cs->pps->isRefScaled( *curPPS );
}

bool CU::isSameSubPic(const CodingUnit& cu, const CodingUnit& cu2)
{
  return (cu.slice->pps->getSubPicFromCU(cu).subPicIdx == cu2.slice->pps->getSubPicFromCU(cu2).subPicIdx) ;
}

bool CU::isSameCtu(const CodingUnit& cu, const CodingUnit& cu2)
{
  uint32_t ctuSizeBit = cu.cs->pcv->maxCUSizeLog2;

  Position pos1Ctu(cu.lumaPos().x  >> ctuSizeBit, cu.lumaPos().y  >> ctuSizeBit);
  Position pos2Ctu(cu2.lumaPos().x >> ctuSizeBit, cu2.lumaPos().y >> ctuSizeBit);

  return pos1Ctu.x == pos2Ctu.x && pos1Ctu.y == pos2Ctu.y;
}

bool CU::isLastSubCUOfCtu( const CodingUnit &cu )
{
  const Area cuAreaY = cu.isSepTree() ? Area( recalcPosition( cu.chromaFormat, cu.chType, CH_L, cu.blocks[cu.chType].pos() ), recalcSize( cu.chromaFormat, cu.chType, CH_L, cu.blocks[cu.chType].size() ) ) : (const Area&)cu.Y();

  return ( ( ( ( cuAreaY.x + cuAreaY.width  ) & cu.cs->pcv->maxCUSizeMask ) == 0 || cuAreaY.x + cuAreaY.width  == cu.cs->pcv->lumaWidth  ) &&
           ( ( ( cuAreaY.y + cuAreaY.height ) & cu.cs->pcv->maxCUSizeMask ) == 0 || cuAreaY.y + cuAreaY.height == cu.cs->pcv->lumaHeight ) );
}

uint32_t CU::getCtuAddr( const CodingUnit &cu )
{
  return getCtuAddr( cu.blocks[cu.chType].lumaPos(), *cu.cs->pcv );
}

int CU::predictQP( const CodingUnit& cu, const int prevQP )
{
  const CodingStructure &cs = *cu.cs;

  uint32_t  ctuRsAddr = getCtuAddr( cu );
  uint32_t  ctuXPosInCtus = ctuRsAddr % cs.pcv->widthInCtus;
  if ( ctuXPosInCtus == 0 &&
      !( cu.blocks[cu.chType].x & ( cs.pcv->maxCUSizeMask >> getChannelTypeScaleX( cu.chType, cu.chromaFormat ) ) ) &&
      !( cu.blocks[cu.chType].y & ( cs.pcv->maxCUSizeMask >> getChannelTypeScaleY( cu.chType, cu.chromaFormat ) ) ) && 
      ( cs.getCU( cu.blocks[cu.chType].pos().offset( 0, -1 ), cu.chType, cu.treeType) != NULL ) && 
      CU::isSameSliceAndTile( *cs.getCU( cu.blocks[cu.chType].pos().offset( 0, -1 ), cu.chType, cu.treeType), cu ) )
  {
    return ( ( cs.getCU( cu.blocks[cu.chType].pos().offset( 0, -1 ), cu.chType, cu.treeType ) )->qp );
  }
  else
  {
    const int a = ( cu.blocks[cu.chType].y & ( cs.pcv->maxCUSizeMask >> getChannelTypeScaleY( cu.chType, cu.chromaFormat ) ) ) ? ( cs.getCU(cu.blocks[cu.chType].pos().offset( 0, -1 ), cu.chType, cu.treeType))->qp : prevQP;
    const int b = ( cu.blocks[cu.chType].x & ( cs.pcv->maxCUSizeMask >> getChannelTypeScaleX( cu.chType, cu.chromaFormat ) ) ) ? ( cs.getCU(cu.blocks[cu.chType].pos().offset( -1, 0 ), cu.chType, cu.treeType))->qp : prevQP;

    return ( a + b + 1 ) >> 1;
  }
}


void CU::addPUs( CodingUnit& cu )
{
  cu.cs->addPU( CS::getArea( *cu.cs, cu, cu.chType, cu.treeType ), cu.chType, &cu );
}

void CU::saveMotionInHMVP( const CodingUnit& cu, const bool isToBeDone )
{
  const PredictionUnit& pu = *cu.pu;

  if (!cu.geo && !cu.affine && !isToBeDone)
  {
    MotionInfo puMi = pu.getMotionInfo();
    HPMVInfo     mi ( puMi, ( puMi.interDir == 3 ) ? cu.BcwIdx : BCW_DEFAULT, cu.imv == IMV_HPEL );

    const unsigned log2ParallelMergeLevel = (pu.cs->sps->log2ParallelMergeLevelMinus2 + 2);
    const unsigned xBr = pu.cu->Y().width + pu.cu->Y().x;
    const unsigned yBr = pu.cu->Y().height + pu.cu->Y().y;
    bool enableHmvp = ((xBr >> log2ParallelMergeLevel) > (pu.cu->Y().x >> log2ParallelMergeLevel)) && ((yBr >> log2ParallelMergeLevel) > (pu.cu->Y().y >> log2ParallelMergeLevel));
    bool enableInsertion = CU::isIBC(cu) || enableHmvp;
    if (enableInsertion)
      cu.cs->addMiToLut( /*CU::isIBC(cu) ? cu.cs->motionLut.lutIbc :*/ cu.cs->motionLut.lut, mi);
  }
}

PartSplit CU::getSplitAtDepth( const CodingUnit& cu, const unsigned depth )
{
  if( depth >= cu.depth ) return CU_DONT_SPLIT;

  const PartSplit cuSplitType = PartSplit( ( cu.splitSeries >> ( depth * SPLIT_DMULT ) ) & SPLIT_MASK );

  if     ( cuSplitType == CU_QUAD_SPLIT    ) return CU_QUAD_SPLIT;

  else if( cuSplitType == CU_HORZ_SPLIT    ) return CU_HORZ_SPLIT;

  else if( cuSplitType == CU_VERT_SPLIT    ) return CU_VERT_SPLIT;

  else if( cuSplitType == CU_TRIH_SPLIT    ) return CU_TRIH_SPLIT;
  else if( cuSplitType == CU_TRIV_SPLIT    ) return CU_TRIV_SPLIT;
  else   { THROW( "Unknown split mode"    ); return CU_QUAD_SPLIT; }
}

ModeType CU::getModeTypeAtDepth( const CodingUnit& cu, const unsigned depth )
{
  ModeType modeType = ModeType( (cu.modeTypeSeries >> (depth * 3)) & 0x07 );
  CHECK( depth > cu.depth, " depth is wrong" );
  return modeType;
}

bool CU::divideTuInRows( const CodingUnit &cu )
{
  CHECK( cu.ispMode != HOR_INTRA_SUBPARTITIONS && cu.ispMode != VER_INTRA_SUBPARTITIONS, "Intra Subpartitions type not recognized!" );
  return cu.ispMode == HOR_INTRA_SUBPARTITIONS ? true : false;
}


PartSplit CU::getISPType( const CodingUnit &cu, const ComponentID compID )
{
  if( cu.ispMode && isLuma( compID ) )
  {
    const bool tuIsDividedInRows = CU::divideTuInRows( cu );

    return tuIsDividedInRows ? TU_1D_HORZ_SPLIT : TU_1D_VERT_SPLIT;
  }
  return TU_NO_ISP;
}

bool CU::isISPLast( const CodingUnit &cu, const CompArea& tuArea, const ComponentID compID )
{
  PartSplit partitionType = CU::getISPType( cu, compID );

  Area originalArea = cu.blocks[compID];
  switch( partitionType )
  {
    case TU_1D_HORZ_SPLIT:
      return tuArea.y + tuArea.height == originalArea.y + originalArea.height;
    case TU_1D_VERT_SPLIT:
      return tuArea.x + tuArea.width == originalArea.x + originalArea.width;
    default:
      THROW( "Unknown ISP processing order type!" );
      return false;
  }
}

bool CU::isISPFirst( const CodingUnit &cu, const CompArea& tuArea, const ComponentID compID )
{
  return tuArea == cu.firstTU->blocks[compID];
}

bool CU::canUseISP( const CodingUnit &cu, const ComponentID compID )
{
  const int width     = cu.blocks[compID].width;
  const int height    = cu.blocks[compID].height;
  const int maxTrSize = cu.cs->sps->getMaxTbSize();
  return CU::canUseISP( width, height, maxTrSize );
}

bool CU::canUseISP( const int width, const int height, const int maxTrSize )
{
  bool  notEnoughSamplesToSplit = ( Log2(width) + Log2(height) <= ( MIN_TB_LOG2_SIZEY << 1 ) );
  bool  cuSizeLargerThanMaxTrSize = width > maxTrSize || height > maxTrSize;
  if ( notEnoughSamplesToSplit || cuSizeLargerThanMaxTrSize )
  {
    return false;
  }
  return true;
}

bool CU::canUseLfnstWithISP( const CompArea& cuArea, const ISPType ispSplitType )
{
  if( ispSplitType == NOT_INTRA_SUBPARTITIONS )
  {
    return false;
  }
  Size tuSize = ( ispSplitType == HOR_INTRA_SUBPARTITIONS ) ? Size( cuArea.width, CU::getISPSplitDim( cuArea.width, cuArea.height, TU_1D_HORZ_SPLIT ) ) :
    Size( CU::getISPSplitDim( cuArea.width, cuArea.height, TU_1D_VERT_SPLIT ), cuArea.height );

  if( !( tuSize.width >= MIN_TB_SIZEY && tuSize.height >= MIN_TB_SIZEY ) )
  {
    return false;
  }
  return true;
}

bool CU::canUseLfnstWithISP( const CodingUnit& cu, const ChannelType chType )
{
  CHECK( !isLuma( chType ), "Wrong ISP mode!" );
  return CU::canUseLfnstWithISP( cu.blocks[chType == CH_L ? 0 : 1], (ISPType)cu.ispMode );
}

uint32_t CU::getISPSplitDim( const int width, const int height, const PartSplit ispType )
{
  bool divideTuInRows = ispType == TU_1D_HORZ_SPLIT;
  uint32_t splitDimensionSize, nonSplitDimensionSize, partitionSize, divShift = 2;

  if( divideTuInRows )
  {
    splitDimensionSize    = height;
    nonSplitDimensionSize = width;
  }
  else
  {
    splitDimensionSize    = width;
    nonSplitDimensionSize = height;
  }

  const int minNumberOfSamplesPerCu = 1 << ( ( MIN_TB_LOG2_SIZEY << 1 ) );
  const int factorToMinSamples = nonSplitDimensionSize < minNumberOfSamplesPerCu ? minNumberOfSamplesPerCu >> Log2(nonSplitDimensionSize) : 1;
  partitionSize = ( splitDimensionSize >> divShift ) < factorToMinSamples ? factorToMinSamples : ( splitDimensionSize >> divShift );

  CHECK( Log2(partitionSize) + Log2(nonSplitDimensionSize) < Log2(minNumberOfSamplesPerCu), "A partition has less than the minimum amount of samples!" );
  return partitionSize;
}

bool CU::allLumaCBFsAreZero(const CodingUnit& cu)
{
  if (!cu.ispMode)
  {
    return TU::getCbf(*cu.firstTU, COMP_Y) == false;
  }
  else
  {
    int numTotalTUs = cu.ispMode == HOR_INTRA_SUBPARTITIONS ? cu.lheight() >> Log2(cu.firstTU->lheight()) : cu.lwidth() >> Log2(cu.firstTU->lwidth());
    TransformUnit* tuPtr = cu.firstTU;
    for (int tuIdx = 0; tuIdx < numTotalTUs; tuIdx++)
    {
      if (TU::getCbf(*tuPtr, COMP_Y) == true)
      {
        return false;
      }
      tuPtr = tuPtr->next;
    }
    return true;
  }
}

TUTraverser CU::traverseTUs( CodingUnit& cu )
{
  return TUTraverser( cu.firstTU, cu.lastTU->next );
}

cTUTraverser CU::traverseTUs( const CodingUnit& cu )
{
  return cTUTraverser( cu.firstTU, cu.lastTU->next );
}

const CodingUnit* CU::getLeft(const CodingUnit& curr)
{
  const Position& pos = curr.blocks[curr.chType].pos();
  return curr.cs->getCU(pos.offset(-1, 0), curr.chType, curr.treeType);
}

const CodingUnit* CU::getAbove(const CodingUnit& curr)
{
  const Position& pos = curr.blocks[curr.chType].pos();
  return curr.cs->getCU(pos.offset(0, -1), curr.chType, curr.treeType);
}

// PU tools

int PU::getIntraMPMs( const PredictionUnit &pu, unsigned* mpm )
{
  const int numMPMs = NUM_MOST_PROBABLE_MODES;
  {
    int numCand      = -1;
    int leftIntraDir = PLANAR_IDX, aboveIntraDir = PLANAR_IDX;

    const CompArea& area = pu.Y();
    const Position posRT = area.topRight();
    const Position posLB = area.bottomLeft();

    // Get intra direction of left PU
    const PredictionUnit *puLeft = pu.cs->getPURestricted(posLB.offset(-1, 0), pu, CH_L);
    if (puLeft && CU::isIntra(*puLeft->cu))
    {
      leftIntraDir = PU::getIntraDirLuma( *puLeft );
    }

    // Get intra direction of above PU
    const PredictionUnit *puAbove = pu.cs->getPURestricted(posRT.offset(0, -1), pu, CH_L);
    if (puAbove && CU::isIntra(*puAbove->cu) && CU::isSameCtu(*pu.cu, *puAbove->cu))
    {
      aboveIntraDir = PU::getIntraDirLuma( *puAbove );
    }

    CHECK(2 >= numMPMs, "Invalid number of most probable modes");

    const int offset = (int)NUM_LUMA_MODE - 6;
    const int mod = offset + 3;

    {
      mpm[0] = PLANAR_IDX;
      mpm[1] = DC_IDX;
      mpm[2] = VER_IDX;
      mpm[3] = HOR_IDX;
      mpm[4] = VER_IDX - 4;
      mpm[5] = VER_IDX + 4;

      if (leftIntraDir == aboveIntraDir)
      {
        numCand = 1;
        if (leftIntraDir > DC_IDX)
        {
          mpm[0] = PLANAR_IDX;
          mpm[1] = leftIntraDir;
          mpm[2] = ((leftIntraDir + offset) % mod) + 2;
          mpm[3] = ((leftIntraDir - 1) % mod) + 2;
          mpm[4] = ((leftIntraDir + offset - 1) % mod) + 2;
          mpm[5] = ( leftIntraDir               % mod) + 2;
        }
      }
      else //L!=A
      {
        numCand = 2;
        int  maxCandModeIdx = mpm[0] > mpm[1] ? 0 : 1;

        if ((leftIntraDir > DC_IDX) && (aboveIntraDir > DC_IDX))
        {
          mpm[0] = PLANAR_IDX;
          mpm[1] = leftIntraDir;
          mpm[2] = aboveIntraDir;
          maxCandModeIdx = mpm[1] > mpm[2] ? 1 : 2;
          int minCandModeIdx = mpm[1] > mpm[2] ? 2 : 1;
          if (mpm[maxCandModeIdx] - mpm[minCandModeIdx] == 1)
          {
            mpm[3] = ((mpm[minCandModeIdx] + offset)     % mod) + 2;
            mpm[4] = ((mpm[maxCandModeIdx] - 1)          % mod) + 2;
            mpm[5] = ((mpm[minCandModeIdx] + offset - 1) % mod) + 2;
          }
          else if (mpm[maxCandModeIdx] - mpm[minCandModeIdx] >= 62)
          {
            mpm[3] = ((mpm[minCandModeIdx] - 1)      % mod) + 2;
            mpm[4] = ((mpm[maxCandModeIdx] + offset) % mod) + 2;
            mpm[5] = ( mpm[minCandModeIdx]           % mod) + 2;
          }
          else if (mpm[maxCandModeIdx] - mpm[minCandModeIdx] == 2)
          {
            mpm[3] = ((mpm[minCandModeIdx] - 1)      % mod) + 2;
            mpm[4] = ((mpm[minCandModeIdx] + offset) % mod) + 2;
            mpm[5] = ((mpm[maxCandModeIdx] - 1)      % mod) + 2;
          }
          else
          {
            mpm[3] = ((mpm[minCandModeIdx] + offset) % mod) + 2;
            mpm[4] = ((mpm[minCandModeIdx] - 1)      % mod) + 2;
            mpm[5] = ((mpm[maxCandModeIdx] + offset) % mod) + 2;
          }
        }
        else if (leftIntraDir + aboveIntraDir >= 2)
        {
          mpm[0] = PLANAR_IDX;
          mpm[1] = (leftIntraDir < aboveIntraDir) ? aboveIntraDir : leftIntraDir;
          maxCandModeIdx = 1;
          mpm[2] = ((mpm[maxCandModeIdx] + offset)     % mod) + 2;
          mpm[3] = ((mpm[maxCandModeIdx] - 1)          % mod) + 2;
          mpm[4] = ((mpm[maxCandModeIdx] + offset - 1) % mod) + 2;
          mpm[5] = ( mpm[maxCandModeIdx]               % mod) + 2;
        }
      }
    }
    for (int i = 0; i < numMPMs; i++)
    {
      CHECK(mpm[i] >= NUM_LUMA_MODE, "Invalid MPM");
    }
    CHECK(numCand == 0, "No candidates found");
    return numCand;
  }
}

bool PU::isMIP(const PredictionUnit &pu, const ChannelType chType)
{
  return chType == CH_L ? pu.cu->mipFlag : ((pu.intraDir[CH_C] == DM_CHROMA_IDX) && isDMChromaMIP(pu));
}

bool PU::isDMChromaMIP(const PredictionUnit &pu)
{
  return !pu.cu->isSepTree() && (pu.chromaFormat == CHROMA_444) && getCoLocatedLumaPU(pu).cu->mipFlag;
}


uint32_t PU::getIntraDirLuma( const PredictionUnit &pu )
{
  if (isMIP(pu))
  {
    return PLANAR_IDX;
  }
  else
  {
    return pu.intraDir[CH_L];
  }
}


void PU::getIntraChromaCandModes( const PredictionUnit &pu, unsigned modeList[NUM_CHROMA_MODE] )
{
  {
    modeList[0] = PLANAR_IDX;
    modeList[1] = VER_IDX;
    modeList[2] = HOR_IDX;
    modeList[3] = DC_IDX;
    modeList[4] = LM_CHROMA_IDX;
    modeList[5] = MDLM_L_IDX;
    modeList[6] = MDLM_T_IDX;
    modeList[7] = DM_CHROMA_IDX;

    // If Direct Mode is MIP, mode cannot be already in the list.
    if (isDMChromaMIP(pu))
    {
      return;
    }

    const uint32_t lumaMode = getCoLocatedIntraLumaMode(pu);
    for( int i = 0; i < 4; i++ )
    {
      if( lumaMode == modeList[i] )
      {
        modeList[i] = VDIA_IDX;
        break;
      }
    }
  }
}

bool PU::isLMCMode(unsigned mode)
{
  return (mode >= LM_CHROMA_IDX && mode <= MDLM_T_IDX);
}

bool PU::isLMCModeEnabled(const PredictionUnit &pu, unsigned mode)
{
  return ( pu.cs->sps->LMChroma && pu.cu->checkCCLMAllowed() );
}

int PU::getLMSymbolList(const PredictionUnit &pu, int *modeList)
{
  int idx = 0;

  modeList[idx++] = LM_CHROMA_IDX;
  modeList[idx++] = MDLM_L_IDX;
  modeList[idx++] = MDLM_T_IDX;
  return idx;
}

bool PU::isChromaIntraModeCrossCheckMode( const PredictionUnit &pu )
{
  return !pu.cu->bdpcmModeChroma && pu.intraDir[CH_C] == DM_CHROMA_IDX;
}

uint32_t PU::getFinalIntraMode( const PredictionUnit &pu, const ChannelType chType )
{
  uint32_t uiIntraMode = pu.intraDir[chType];

  if( uiIntraMode == DM_CHROMA_IDX && !isLuma( chType ) )
  {
    uiIntraMode = getCoLocatedIntraLumaMode(pu);
  }
  if( pu.chromaFormat == CHROMA_422 && !isLuma( chType ) && uiIntraMode < NUM_LUMA_MODE ) // map directional, planar and dc
  {
    uiIntraMode = g_chroma422IntraAngleMappingTable[uiIntraMode];
  }
  return uiIntraMode;
}

const PredictionUnit &PU::getCoLocatedLumaPU(const PredictionUnit &pu)
{
  Position              topLeftPos = pu.blocks[pu.chType].lumaPos();
  Position              refPos     = topLeftPos.offset(pu.blocks[pu.chType].lumaSize().width  >> 1,
                                                       pu.blocks[pu.chType].lumaSize().height >> 1);
  const PredictionUnit &lumaPU     = pu.cu->isSepTree() ? *pu.cs->refCS->getPU(refPos, CH_L)
                                                        : *pu.cs->getPU(topLeftPos, CH_L);

  return lumaPU;
}

uint32_t PU::getCoLocatedIntraLumaMode(const PredictionUnit &pu)
{
  return PU::getIntraDirLuma(PU::getCoLocatedLumaPU(pu));
}

bool PU::addMergeHMVPCand(const CodingStructure &cs, MergeCtx& mrgCtx, const int& mrgCandIdx, const uint32_t maxNumMergeCandMin1, int &cnt, const bool isAvailableA1, const MotionInfo& miLeft, const bool isAvailableB1, const MotionInfo& miAbove, const bool ibcFlag, const bool isGt4x4)
{
  const Slice& slice = *cs.slice;
  HPMVInfo miNeighbor;

  auto &lut = /*ibcFlag ? cs.motionLut.lutIbc :*/ cs.motionLut.lut;
  int num_avai_candInLUT = (int) lut.size();

  for (int mrgIdx = 1; mrgIdx <= num_avai_candInLUT; mrgIdx++)
  {
    miNeighbor = lut[num_avai_candInLUT - mrgIdx];

    if ( mrgIdx > 2 || ((mrgIdx > 1 || !isGt4x4) && ibcFlag)
      || ((!isAvailableA1 || (miNeighbor != miLeft)) && (!isAvailableB1 || (miNeighbor != miAbove))) )
    {
      mrgCtx.interDirNeighbours[cnt] = miNeighbor.interDir;
      mrgCtx.useAltHpelIf      [cnt] = !ibcFlag && miNeighbor.useAltHpelIf;
      mrgCtx.BcwIdx            [cnt] = (miNeighbor.interDir == 3) ? miNeighbor.BcwIdx : BCW_DEFAULT;
      mrgCtx.mvFieldNeighbours[cnt << 1].setMvField(miNeighbor.mv[0], miNeighbor.refIdx[0]);

      if (slice.isInterB())
      {
        mrgCtx.mvFieldNeighbours[(cnt << 1) + 1].setMvField(miNeighbor.mv[1], miNeighbor.refIdx[1]);
      }

      if (mrgCandIdx == cnt)
      {
        return true;
      }
      cnt ++;

      if (cnt  == maxNumMergeCandMin1)
      {
        break;
      }
    }
  }

  if (cnt < maxNumMergeCandMin1)
  {
    mrgCtx.useAltHpelIf[cnt] = false;
  }

  return false;
}


void PU::getInterMergeCandidates( const PredictionUnit &pu, MergeCtx& mrgCtx, int mmvdList, const int mrgCandIdx )
{
  const unsigned plevel = pu.cs->sps->log2ParallelMergeLevelMinus2 + 2;
  const CodingStructure &cs  = *pu.cs;
  const Slice &slice         = *pu.cs->slice;
  const uint32_t maxNumMergeCand = slice.sps->maxNumMergeCand;

  for (uint32_t ui = 0; ui < maxNumMergeCand; ++ui)
  {
    mrgCtx.BcwIdx[ui] = BCW_DEFAULT;
    mrgCtx.interDirNeighbours[ui] = 0;
    mrgCtx.mrgTypeNeighbours [ui] = MRG_TYPE_DEFAULT_N;
    mrgCtx.mvFieldNeighbours[(ui << 1)    ].refIdx = NOT_VALID;
    mrgCtx.mvFieldNeighbours[(ui << 1) + 1].refIdx = NOT_VALID;
    mrgCtx.useAltHpelIf[ui] = false;
  }

  mrgCtx.numValidMergeCand = maxNumMergeCand;
  // compute the location of the current PU

  int cnt = 0;

  const Position posLT = pu.Y().topLeft();
  const Position posRT = pu.Y().topRight();
  const Position posLB = pu.Y().bottomLeft();
  MotionInfo miAbove, miLeft, miAboveLeft, miAboveRight, miBelowLeft;

  // above
  const PredictionUnit *puAbove = cs.getPURestricted(posRT.offset(0, -1), pu, pu.chType);

  bool isAvailableB1 = puAbove && isDiffMER(pu.lumaPos(), posRT.offset(0, -1), plevel) && pu.cu != puAbove->cu && CU::isInter(*puAbove->cu);

  if (isAvailableB1)
  {
    miAbove = puAbove->getMotionInfo(posRT.offset(0, -1));

    // get Inter Dir
    mrgCtx.interDirNeighbours[cnt] = miAbove.interDir;
    mrgCtx.useAltHpelIf[cnt] = puAbove->cu->imv == IMV_HPEL;
    // get Mv from Above
    mrgCtx.BcwIdx[cnt] = (mrgCtx.interDirNeighbours[cnt] == 3) ? puAbove->cu->BcwIdx : BCW_DEFAULT;
    mrgCtx.mvFieldNeighbours[cnt << 1].setMvField(miAbove.mv[0], miAbove.refIdx[0]);

    if (slice.isInterB())
    {
      mrgCtx.mvFieldNeighbours[(cnt << 1) + 1].setMvField(miAbove.mv[1], miAbove.refIdx[1]);
    }
    if (mrgCandIdx == cnt)
    {
      return;
    }

    cnt++;
  }

  // early termination
  if (cnt == maxNumMergeCand)
  {
    return;
  }

  //left
  const PredictionUnit* puLeft = cs.getPURestricted(posLB.offset(-1, 0), pu, pu.chType);

  const bool isAvailableA1 = puLeft && isDiffMER(pu.lumaPos(), posLB.offset(-1, 0), plevel) && pu.cu != puLeft->cu && CU::isInter(*puLeft->cu);

  if (isAvailableA1)
  {
    miLeft = puLeft->getMotionInfo(posLB.offset(-1, 0));

    if (!isAvailableB1 || (miAbove != miLeft))
    {
      // get Inter Dir
      mrgCtx.interDirNeighbours[cnt] = miLeft.interDir;
      mrgCtx.useAltHpelIf[cnt] = puLeft->cu->imv == IMV_HPEL;
      mrgCtx.BcwIdx[cnt] = (mrgCtx.interDirNeighbours[cnt] == 3) ? puLeft->cu->BcwIdx : BCW_DEFAULT;
      // get Mv from Left
      mrgCtx.mvFieldNeighbours[cnt << 1].setMvField(miLeft.mv[0], miLeft.refIdx[0]);

      if (slice.isInterB())
      {
        mrgCtx.mvFieldNeighbours[(cnt << 1) + 1].setMvField(miLeft.mv[1], miLeft.refIdx[1]);
      }
      if (mrgCandIdx == cnt)
      {
        return;
      }

      cnt++;
    }
  }

  // early termination
  if( cnt == maxNumMergeCand )
  {
    return;
  }

  // above right
  const PredictionUnit *puAboveRight = cs.getPURestricted( posRT.offset( 1, -1 ), pu, pu.chType );

  bool isAvailableB0 = puAboveRight && isDiffMER( pu.lumaPos(), posRT.offset(1, -1), plevel) && CU::isInter( *puAboveRight->cu );

  if( isAvailableB0 )
  {
    miAboveRight = puAboveRight->getMotionInfo( posRT.offset( 1, -1 ) );

    if( !isAvailableB1 || ( miAbove != miAboveRight ) )
    {
      // get Inter Dir
      mrgCtx.interDirNeighbours[cnt] = miAboveRight.interDir;
      mrgCtx.useAltHpelIf[cnt] = puAboveRight->cu->imv == IMV_HPEL;
      // get Mv from Above-right
      mrgCtx.BcwIdx[cnt] = (mrgCtx.interDirNeighbours[cnt] == 3) ? puAboveRight->cu->BcwIdx : BCW_DEFAULT;
      mrgCtx.mvFieldNeighbours[cnt << 1].setMvField( miAboveRight.mv[0], miAboveRight.refIdx[0] );

      if( slice.isInterB() )
      {
        mrgCtx.mvFieldNeighbours[( cnt << 1 ) + 1].setMvField( miAboveRight.mv[1], miAboveRight.refIdx[1] );
      }

      if (mrgCandIdx == cnt)
      {
        return;
      }

      cnt++;
    }
  }
  // early termination
  if( cnt == maxNumMergeCand )
  {
    return;
  }

  //left bottom
  const PredictionUnit *puLeftBottom = cs.getPURestricted( posLB.offset( -1, 1 ), pu, pu.chType );

  bool isAvailableA0 = puLeftBottom && isDiffMER( pu.lumaPos(), posLB.offset(-1, 1), plevel) && CU::isInter( *puLeftBottom->cu );

  if( isAvailableA0 )
  {
    miBelowLeft = puLeftBottom->getMotionInfo( posLB.offset( -1, 1 ) );

    if( !isAvailableA1 || ( miBelowLeft != miLeft ) )
    {
      // get Inter Dir
      mrgCtx.interDirNeighbours[cnt] = miBelowLeft.interDir;
      mrgCtx.useAltHpelIf[cnt] = puLeftBottom->cu->imv == IMV_HPEL;
      mrgCtx.BcwIdx[cnt] = (mrgCtx.interDirNeighbours[cnt] == 3) ? puLeftBottom->cu->BcwIdx : BCW_DEFAULT;
      // get Mv from Bottom-Left
      mrgCtx.mvFieldNeighbours[cnt << 1].setMvField( miBelowLeft.mv[0], miBelowLeft.refIdx[0] );

      if( slice.isInterB() )
      {
        mrgCtx.mvFieldNeighbours[( cnt << 1 ) + 1].setMvField( miBelowLeft.mv[1], miBelowLeft.refIdx[1] );
      }

      if (mrgCandIdx == cnt)
      {
        return;
      }

      cnt++;
    }
  }
  // early termination
  if( cnt == maxNumMergeCand )
  {
    return;
  }


  // above left
  if ( cnt < 4 )
  {
    const PredictionUnit *puAboveLeft = cs.getPURestricted( posLT.offset( -1, -1 ), pu, pu.chType );

    bool isAvailableB2 = puAboveLeft && isDiffMER( pu.lumaPos(), posLT.offset(-1, -1), plevel ) && CU::isInter( *puAboveLeft->cu );

    if( isAvailableB2 )
    {
      miAboveLeft = puAboveLeft->getMotionInfo( posLT.offset( -1, -1 ) );

      if( ( !isAvailableA1 || ( miLeft != miAboveLeft ) ) && ( !isAvailableB1 || ( miAbove != miAboveLeft ) ) )
      {
        // get Inter Dir
        mrgCtx.interDirNeighbours[cnt] = miAboveLeft.interDir;
        mrgCtx.useAltHpelIf[cnt] = puAboveLeft->cu->imv == IMV_HPEL;
        mrgCtx.BcwIdx[cnt] = (mrgCtx.interDirNeighbours[cnt] == 3) ? puAboveLeft->cu->BcwIdx : BCW_DEFAULT;
        // get Mv from Above-Left
        mrgCtx.mvFieldNeighbours[cnt << 1].setMvField( miAboveLeft.mv[0], miAboveLeft.refIdx[0] );

        if( slice.isInterB() )
        {
          mrgCtx.mvFieldNeighbours[( cnt << 1 ) + 1].setMvField( miAboveLeft.mv[1], miAboveLeft.refIdx[1] );
        }

        if (mrgCandIdx == cnt)
        {
          return;
        }

        cnt++;
      }
    }
  }
  // early termination
  if (cnt == maxNumMergeCand)
  {
    return;
  }

  if (slice.picHeader->enableTMVP && (pu.lumaSize().width + pu.lumaSize().height > 12))
  {
    //>> MTK colocated-RightBottom
    // offset the pos to be sure to "point" to the same position the uiAbsPartIdx would've pointed to
    Position posRB = pu.Y().bottomRight().offset( -3, -3 );
    const PreCalcValues& pcv = *cs.pcv;

    Position posC0;
    Position posC1 = pu.Y().center();
    bool C0Avail = false;
    bool boundaryCond = ((posRB.x + pcv.minCUSize) < pcv.lumaWidth) && ((posRB.y + pcv.minCUSize) < pcv.lumaHeight);
    SubPic curSubPic = pu.cs->slice->pps->getSubPicFromPos(pu.lumaPos());
    if (curSubPic.treatedAsPic )
    {
      boundaryCond = ((posRB.x + pcv.minCUSize) <= curSubPic.subPicRight &&
                      (posRB.y + pcv.minCUSize) <= curSubPic.subPicBottom);
    }    
    if (boundaryCond)
    {
      int posYInCtu = posRB.y & pcv.maxCUSizeMask;
      if (posYInCtu + 4 < pcv.maxCUSize)
      {
        posC0 = posRB.offset(4, 4);
        C0Avail = true;
      }
    }

    Mv        cColMv;
    int       iRefIdx     = 0;
    int       dir         = 0;
    unsigned  uiArrayAddr = cnt;
    bool      bExistMV    = ( C0Avail && getColocatedMVP(pu, REF_PIC_LIST_0, posC0, cColMv, iRefIdx ) )
                              || getColocatedMVP( pu, REF_PIC_LIST_0, posC1, cColMv, iRefIdx );
    if (bExistMV)
    {
      dir     |= 1;
      mrgCtx.mvFieldNeighbours[2 * uiArrayAddr].setMvField(cColMv, iRefIdx);
    }

    if (slice.isInterB())
    {
      bExistMV = ( C0Avail && getColocatedMVP(pu, REF_PIC_LIST_1, posC0, cColMv, iRefIdx ) )
                   || getColocatedMVP( pu, REF_PIC_LIST_1, posC1, cColMv, iRefIdx );
      if (bExistMV)
      {
        dir     |= 2;
        mrgCtx.mvFieldNeighbours[2 * uiArrayAddr + 1].setMvField(cColMv, iRefIdx);
      }
    }

    if( dir != 0 )
    {
      bool addTMvp = true;
      if( addTMvp )
      {
        mrgCtx.interDirNeighbours[uiArrayAddr] = dir;
        mrgCtx.BcwIdx[uiArrayAddr] = BCW_DEFAULT;
        mrgCtx.useAltHpelIf[uiArrayAddr] = false;
        if (mrgCandIdx == cnt)
        {
          return;
        }

        cnt++;
      }
    }
  }

  // early termination
  if (cnt == maxNumMergeCand)
  {
    return;
  }

  int maxNumMergeCandMin1 = maxNumMergeCand - 1;
  if (cnt != maxNumMergeCandMin1)
  {
    bool isGt4x4 = true;
    bool bFound = addMergeHMVPCand(cs, mrgCtx, mrgCandIdx, maxNumMergeCandMin1, cnt, isAvailableA1, miLeft, isAvailableB1, miAbove, CU::isIBC(*pu.cu), isGt4x4);
    if (bFound)
    {
      return;
    }
  }

  // pairwise-average candidates
  {
    if (cnt > 1 && cnt < maxNumMergeCand)
    {
      mrgCtx.mvFieldNeighbours[cnt * 2].setMvField( Mv( 0, 0 ), NOT_VALID );
      mrgCtx.mvFieldNeighbours[cnt * 2 + 1].setMvField( Mv( 0, 0 ), NOT_VALID );
      // calculate average MV for L0 and L1 seperately
      unsigned char interDir = 0;


      mrgCtx.useAltHpelIf[cnt] = (mrgCtx.useAltHpelIf[0] == mrgCtx.useAltHpelIf[1]) ? mrgCtx.useAltHpelIf[0] : false;
      for( int refListId = 0; refListId < (slice.isInterB() ? 2 : 1); refListId++ )
      {
        const short refIdxI = mrgCtx.mvFieldNeighbours[0 * 2 + refListId].refIdx;
        const short refIdxJ = mrgCtx.mvFieldNeighbours[1 * 2 + refListId].refIdx;

        // both MVs are invalid, skip
        if( (refIdxI == NOT_VALID) && (refIdxJ == NOT_VALID) )
        {
          continue;
        }

        interDir += 1 << refListId;
        // both MVs are valid, average these two MVs
        if( (refIdxI != NOT_VALID) && (refIdxJ != NOT_VALID) )
        {
          const Mv& MvI = mrgCtx.mvFieldNeighbours[0 * 2 + refListId].mv;
          const Mv& MvJ = mrgCtx.mvFieldNeighbours[1 * 2 + refListId].mv;

          // average two MVs
          Mv avgMv = MvI;
          avgMv += MvJ;
          roundAffineMv(avgMv.hor, avgMv.ver, 1);

          mrgCtx.mvFieldNeighbours[cnt * 2 + refListId].setMvField( avgMv, refIdxI );
        }
        // only one MV is valid, take the only one MV
        else if( refIdxI != NOT_VALID )
        {
          Mv singleMv = mrgCtx.mvFieldNeighbours[0 * 2 + refListId].mv;
          mrgCtx.mvFieldNeighbours[cnt * 2 + refListId].setMvField( singleMv, refIdxI );
        }
        else if( refIdxJ != NOT_VALID )
        {
          Mv singleMv = mrgCtx.mvFieldNeighbours[1 * 2 + refListId].mv;
          mrgCtx.mvFieldNeighbours[cnt * 2 + refListId].setMvField( singleMv, refIdxJ );
        }
      }

      mrgCtx.interDirNeighbours[cnt] = interDir;
      if( interDir > 0 )
      {
        cnt++;
      }
    }

    // early termination
    if( cnt == maxNumMergeCand )
    {
      return;
    }
  }

  uint32_t uiArrayAddr = cnt;

  int iNumRefIdx = slice.isInterB() ? std::min(slice.numRefIdx[ REF_PIC_LIST_0 ], slice.numRefIdx[ REF_PIC_LIST_1 ]) : slice.numRefIdx[ REF_PIC_LIST_0 ];

  int r = 0;
  int refcnt = 0;
  while (uiArrayAddr < maxNumMergeCand)
  {
    mrgCtx.interDirNeighbours [uiArrayAddr     ] = 1;
    mrgCtx.BcwIdx             [uiArrayAddr     ] = BCW_DEFAULT;
    mrgCtx.mvFieldNeighbours  [uiArrayAddr << 1].setMvField(Mv(0, 0), r);
    mrgCtx.useAltHpelIf[uiArrayAddr] = false;

    if (slice.isInterB())
    {
      mrgCtx.interDirNeighbours [ uiArrayAddr          ] = 3;
      mrgCtx.mvFieldNeighbours  [(uiArrayAddr << 1) + 1].setMvField(Mv(0, 0), r);
    }

    if ( mrgCtx.interDirNeighbours[uiArrayAddr] == 1 && pu.cs->slice->getRefPic(REF_PIC_LIST_0, mrgCtx.mvFieldNeighbours[uiArrayAddr << 1].refIdx)->getPOC() == pu.cs->slice->poc)
    {
      THROW("no IBC support");
    }

    uiArrayAddr++;

    if (refcnt == iNumRefIdx - 1)
    {
      r = 0;
    }
    else
    {
      ++r;
      ++refcnt;
    }
  }
  mrgCtx.numValidMergeCand = uiArrayAddr;
}

bool PU::checkDMVRCondition(const PredictionUnit& pu)
{
  if (!pu.cs->sps->DMVR || pu.cs->slice->picHeader->disDmvrFlag)
  {
    return false;
  }

  return pu.mergeFlag
    && pu.mergeType == MRG_TYPE_DEFAULT_N
    && !pu.ciip
    && !pu.cu->affine
    && !pu.mmvdMergeFlag
    && !pu.cu->mmvdSkip
    && PU::isBiPredFromDifferentDirEqDistPoc(pu)
    && (pu.lheight() >= 8)
    && (pu.lwidth() >= 8)
    && ((pu.lheight() * pu.lwidth()) >= 128)
    && (pu.cu->BcwIdx == BCW_DEFAULT);
}

int convertMvFixedToFloat(int32_t val)
{
  int sign  = val >> 31;
  int scale = floorLog2((val ^ sign) | MV_MANTISSA_UPPER_LIMIT) - (MV_MANTISSA_BITCOUNT - 1);

  int exponent;
  int mantissa;
  if (scale >= 0)
  {
    int round = (1 << scale) >> 1;
    int n     = (val + round) >> scale;
    exponent  = scale + ((n ^ sign) >> (MV_MANTISSA_BITCOUNT - 1));
    mantissa  = (n & MV_MANTISSA_UPPER_LIMIT) | (sign << (MV_MANTISSA_BITCOUNT - 1));
  }
  else
  {
    exponent = 0;
    mantissa = val;
  }

  return exponent | (mantissa << MV_EXPONENT_BITCOUNT);
}

int convertMvFloatToFixed(int val)
{
  int exponent = val & MV_EXPONENT_MASK;
  int mantissa = val >> MV_EXPONENT_BITCOUNT;
  return exponent == 0 ? mantissa : (mantissa ^ MV_MANTISSA_LIMIT) << (exponent - 1);
}

int roundMvComp(int x)
{
  return convertMvFloatToFixed(convertMvFixedToFloat(x));
}

int PU::getDistScaleFactor(const int currPOC, const int currRefPOC, const int colPOC, const int colRefPOC)
{
  int iDiffPocD = colPOC - colRefPOC;
  int iDiffPocB = currPOC - currRefPOC;

  if (iDiffPocD == iDiffPocB)
  {
    return 4096;
  }
  else
  {
    int iTDB = Clip3(-128, 127, iDiffPocB);
    int iTDD = Clip3(-128, 127, iDiffPocD);
    int iX = (0x4000 + abs(iTDD / 2)) / iTDD;
    int iScale = Clip3(-4096, 4095, (iTDB * iX + 32) >> 6);
    return iScale;
  }
}

void PU::getInterMMVDMergeCandidates(const PredictionUnit &pu, MergeCtx& mrgCtx, const int& mrgCandIdx)
{
  int refIdxList0, refIdxList1;
  int k;
  int currBaseNum = 0;
  const uint16_t maxNumMergeCand = mrgCtx.numValidMergeCand;

  for (k = 0; k < maxNumMergeCand; k++)
  {
    if (mrgCtx.mrgTypeNeighbours[k] == MRG_TYPE_DEFAULT_N)
    {
      refIdxList0 = mrgCtx.mvFieldNeighbours[(k << 1)].refIdx;
      refIdxList1 = mrgCtx.mvFieldNeighbours[(k << 1) + 1].refIdx;

      if ((refIdxList0 >= 0) && (refIdxList1 >= 0))
      {
        mrgCtx.mmvdBaseMv[currBaseNum][0] = mrgCtx.mvFieldNeighbours[(k << 1)];
        mrgCtx.mmvdBaseMv[currBaseNum][1] = mrgCtx.mvFieldNeighbours[(k << 1) + 1];
      }
      else if (refIdxList0 >= 0)
      {
        mrgCtx.mmvdBaseMv[currBaseNum][0] = mrgCtx.mvFieldNeighbours[(k << 1)];
        mrgCtx.mmvdBaseMv[currBaseNum][1] = MvField(Mv(0, 0), -1);
      }
      else if (refIdxList1 >= 0)
      {
        mrgCtx.mmvdBaseMv[currBaseNum][0] = MvField(Mv(0, 0), -1);
        mrgCtx.mmvdBaseMv[currBaseNum][1] = mrgCtx.mvFieldNeighbours[(k << 1) + 1];
      }
      mrgCtx.mmvdUseAltHpelIf[currBaseNum] = mrgCtx.useAltHpelIf[k];

      currBaseNum++;

      if (currBaseNum == MMVD_BASE_MV_NUM)
        break;
    }
  }
}

bool PU::getColocatedMVP(const PredictionUnit &pu, const RefPicList refPicList, const Position& _pos, Mv& rcMv, const int refIdx, bool sbFlag )
{
  // don't perform MV compression when generally disabled or SbTMVP is used
  const unsigned scale = 4 * std::max<int>(1, 4 * AMVP_DECIMATION_FACTOR / 4);
  const unsigned mask  = ~( scale - 1 );

  const Position pos = Position{ PosType( _pos.x & mask ), PosType( _pos.y & mask ) };

  const Slice &slice = *pu.cs->slice;

  // use coldir.
  const Picture* const pColPic = slice.getRefPic(RefPicList(slice.isInterB() ? 1 - slice.colFromL0Flag : 0), slice.colRefIdx);

  if( !pColPic )
  {
    return false;
  }

  // Check the position of colocated block is within a subpicture
  SubPic curSubPic = pu.cs->slice->pps->getSubPicFromPos(pu.lumaPos());
  if (curSubPic.treatedAsPic)
  {
    if (!curSubPic.isContainingPos(pos))
      return false;
  }

  RefPicList eColRefPicList = slice.checkLDC ? refPicList : RefPicList(slice.colFromL0Flag);

  const MotionInfo& mi = pColPic->cs->getMotionInfo( pos );

  if( !mi.isInter )
  {
    return false;
  }
  if (CU::isIBC(*pu.cu))
  {
    return false;
  }
  int iColRefIdx = mi.refIdx[eColRefPicList];

  if (sbFlag && !slice.checkLDC)
  {
    eColRefPicList = refPicList;
    iColRefIdx = mi.refIdx[eColRefPicList];
    if (iColRefIdx < 0)
    {
      return false;
    }
  }
  else
  {
    if (iColRefIdx < 0)
    {
      eColRefPicList = RefPicList(1 - eColRefPicList);
      iColRefIdx = mi.refIdx[eColRefPicList];

      if (iColRefIdx < 0)
      {
        return false;
      }
    }
  }

  const Slice* sliceCol = nullptr;

  for( const auto s : pColPic->slices )
  {
    if( s->independentSliceIdx == mi.sliceIdx )
    {
      sliceCol = s;
      break;
    }
  }

  CHECK( sliceCol == nullptr, "Slice segment not found" );

  const bool bIsCurrRefLongTerm = slice.getRefPic(refPicList, refIdx)->isLongTerm;
  const bool bIsColRefLongTerm  = sliceCol->isUsedAsLongTerm[eColRefPicList][iColRefIdx];

  if (bIsCurrRefLongTerm != bIsColRefLongTerm)
  {
    return false;
  }


  // Scale the vector.
  Mv cColMv = mi.mv[eColRefPicList];
  cColMv.hor = roundMvComp( cColMv.hor );
  cColMv.ver = roundMvComp( cColMv.ver );

  if (bIsCurrRefLongTerm)
  {
    rcMv = cColMv;
    rcMv.clipToStorageBitDepth();
  }
  else
  {
    const int currPOC    = slice.poc;
    const int colPOC     = sliceCol->poc;
    const int colRefPOC  = sliceCol->getRefPOC(eColRefPicList, iColRefIdx);
    const int currRefPOC = slice.getRefPic(refPicList, refIdx)->getPOC();
    const int distscale  = getDistScaleFactor(currPOC, currRefPOC, colPOC, colRefPOC);

    if (distscale == 4096)
    {
      rcMv = cColMv;
      rcMv.clipToStorageBitDepth();
    }
    else
    {
      rcMv = cColMv.scaleMv(distscale);
    }
  }

  return true;
}

bool PU::isDiffMER(const Position &pos1, const Position &pos2, const unsigned plevel)
{
  const unsigned xN = pos1.x;
  const unsigned yN = pos1.y;
  const unsigned xP = pos2.x;
  const unsigned yP = pos2.y;

  if ((xN >> plevel) != (xP >> plevel))
  {
    return true;
  }

  if ((yN >> plevel) != (yP >> plevel))
  {
    return true;
  }

  return false;
}


/** Constructs a list of candidates for AMVP (See specification, section "Derivation process for motion vector predictor candidates")
* \param uiPartIdx
* \param uiPartAddr
* \param refPicList
* \param iRefIdx
* \param pInfo
*/
void PU::fillMvpCand(PredictionUnit &pu, const RefPicList refPicList, const int refIdx, AMVPInfo &amvpInfo)
{
  CodingStructure &cs = *pu.cs;

  AMVPInfo *pInfo = &amvpInfo;

  pInfo->numCand = 0;

  if (refIdx < 0)
  {
    return;
  }

  //-- Get Spatial MV
  Position posLT = pu.Y().topLeft();
  Position posRT = pu.Y().topRight();
  Position posLB = pu.Y().bottomLeft();

  {
    bool bAdded = addMVPCandUnscaled( pu, refPicList, refIdx, posLB, MD_BELOW_LEFT, *pInfo );

    if( !bAdded )
    {
      bAdded = addMVPCandUnscaled( pu, refPicList, refIdx, posLB, MD_LEFT, *pInfo );

    }
  }

  // Above predictor search
  {
    bool bAdded = addMVPCandUnscaled( pu, refPicList, refIdx, posRT, MD_ABOVE_RIGHT, *pInfo );

    if( !bAdded )
    {
      bAdded = addMVPCandUnscaled( pu, refPicList, refIdx, posRT, MD_ABOVE, *pInfo );

      if( !bAdded )
      {
        addMVPCandUnscaled( pu, refPicList, refIdx, posLT, MD_ABOVE_LEFT, *pInfo );
      }
    }
  }


  for( int i = 0; i < pInfo->numCand; i++ )
  {
    pInfo->mvCand[i].roundTransPrecInternal2Amvr(pu.cu->imv);
  }

  if( pInfo->numCand == 2 )
  {
    if( pInfo->mvCand[0] == pInfo->mvCand[1] )
    {
      pInfo->numCand = 1;
    }
  }

  if (cs.picHeader->enableTMVP && pInfo->numCand < AMVP_MAX_NUM_CANDS && (pu.lumaSize().width + pu.lumaSize().height > 12))
  {
    // Get Temporal Motion Predictor
    const int refIdx_Col = refIdx;

    Position posRB = pu.Y().bottomRight().offset(-3, -3);

    const PreCalcValues& pcv = *cs.pcv;

    Position posC0;
    bool C0Avail = false;
    Position posC1 = pu.Y().center();
    Mv cColMv;

    bool boundaryCond = ((posRB.x + pcv.minCUSize) < pcv.lumaWidth) && ((posRB.y + pcv.minCUSize) < pcv.lumaHeight);
    SubPic curSubPic = pu.cs->slice->pps->getSubPicFromPos(pu.lumaPos());
    if (curSubPic.treatedAsPic)
    {
      boundaryCond = ((posRB.x + pcv.minCUSize) <= curSubPic.subPicRight &&
                      (posRB.y + pcv.minCUSize) <= curSubPic.subPicBottom);
    }    
    if (boundaryCond)
    {
      int posYInCtu = posRB.y & pcv.maxCUSizeMask;
      if (posYInCtu + 4 < pcv.maxCUSize)
      {
        posC0 = posRB.offset(4, 4);
        C0Avail = true;
      }
    }
    if ( ( C0Avail && getColocatedMVP( pu, refPicList, posC0, cColMv, refIdx_Col ) ) || getColocatedMVP( pu, refPicList, posC1, cColMv, refIdx_Col ) )
    {
      cColMv.roundTransPrecInternal2Amvr(pu.cu->imv);
      pInfo->mvCand[pInfo->numCand++] = cColMv;
    }
  }

  if (pInfo->numCand < AMVP_MAX_NUM_CANDS)
  {
    const int        currRefPOC = cs.slice->getRefPic(refPicList, refIdx)->getPOC();
    addAMVPHMVPCand(pu, refPicList, currRefPOC, *pInfo);
  }

  if (pInfo->numCand > AMVP_MAX_NUM_CANDS)
  {
    pInfo->numCand = AMVP_MAX_NUM_CANDS;
  }

  while (pInfo->numCand < AMVP_MAX_NUM_CANDS)
  {
    pInfo->mvCand[pInfo->numCand] = Mv( 0, 0 );
    pInfo->numCand++;
  }

  for (Mv &mv : pInfo->mvCand)
  {
    mv.roundTransPrecInternal2Amvr(pu.cu->imv);
  }
}

bool PU::addAffineMVPCandUnscaled(const PredictionUnit &pu, const RefPicList refPicList, const int refIdx, const Position& pos, const MvpDir dir, AffineAMVPInfo &affiAMVPInfo)
{
  CodingStructure &cs = *pu.cs;
  const PredictionUnit *neibPU = NULL;
  Position neibPos;

  switch (dir)
  {
  case MD_LEFT:
    neibPos = pos.offset(-1, 0);
    break;
  case MD_ABOVE:
    neibPos = pos.offset(0, -1);
    break;
  case MD_ABOVE_RIGHT:
    neibPos = pos.offset(1, -1);
    break;
  case MD_BELOW_LEFT:
    neibPos = pos.offset(-1, 1);
    break;
  case MD_ABOVE_LEFT:
    neibPos = pos.offset(-1, -1);
    break;
  default:
    break;
  }

  neibPU = cs.getPURestricted(neibPos, pu, pu.chType);

  if (neibPU == NULL || !CU::isInter(*neibPU->cu) || !neibPU->cu->affine
    || neibPU->mergeType != MRG_TYPE_DEFAULT_N
    )
  {
    return false;
  }

  Mv outputAffineMv[3];
  const MotionInfo& neibMi = neibPU->getMotionInfo(neibPos);

  const int        currRefPOC = cs.slice->getRefPic(refPicList, refIdx)->getPOC();
  const RefPicList refPicList2nd = (refPicList == REF_PIC_LIST_0) ? REF_PIC_LIST_1 : REF_PIC_LIST_0;

  for (int predictorSource = 0; predictorSource < 2; predictorSource++) // examine the indicated reference picture list, then if not available, examine the other list.
  {
    const RefPicList refPicListIndex = (predictorSource == 0) ? refPicList : refPicList2nd;
    const int        neibRefIdx = neibMi.refIdx[refPicListIndex];

    if (((neibPU->interDir & (refPicListIndex + 1)) == 0) || pu.cu->slice->getRefPOC(refPicListIndex, neibRefIdx) != currRefPOC)
    {
      continue;
    }

    xInheritedAffineMv(pu, neibPU, refPicListIndex, outputAffineMv);
    outputAffineMv[0].roundAffinePrecInternal2Amvr(pu.cu->imv);
    outputAffineMv[1].roundAffinePrecInternal2Amvr(pu.cu->imv);
    affiAMVPInfo.mvCandLT[affiAMVPInfo.numCand] = outputAffineMv[0];
    affiAMVPInfo.mvCandRT[affiAMVPInfo.numCand] = outputAffineMv[1];
    if (pu.cu->affineType == AFFINEMODEL_6PARAM)
    {
      outputAffineMv[2].roundAffinePrecInternal2Amvr(pu.cu->imv);
      affiAMVPInfo.mvCandLB[affiAMVPInfo.numCand] = outputAffineMv[2];
    }
    affiAMVPInfo.numCand++;
    return true;
  }

  return false;
}

void PU::xInheritedAffineMv(const PredictionUnit &pu, const PredictionUnit* puNeighbour, RefPicList refPicList, Mv rcMv[3])
{
  int posNeiX = puNeighbour->Y().pos().x;
  int posNeiY = puNeighbour->Y().pos().y;
  int posCurX = pu.Y().pos().x;
  int posCurY = pu.Y().pos().y;

  int neiW = puNeighbour->Y().width;
  int curW = pu.Y().width;
  int neiH = puNeighbour->Y().height;
  int curH = pu.Y().height;

  Mv mvLT, mvRT, mvLB;
  mvLT = puNeighbour->mvAffi[refPicList][0];
  mvRT = puNeighbour->mvAffi[refPicList][1];
  mvLB = puNeighbour->mvAffi[refPicList][2];

  bool isTopCtuBoundary = false;
  if ((posNeiY + neiH) % pu.cs->sps->CTUSize == 0 && (posNeiY + neiH) == posCurY)
  {
    // use bottom-left and bottom-right sub-block MVs for inheritance
    const Position posRB = puNeighbour->Y().bottomRight();
    const Position posLB = puNeighbour->Y().bottomLeft();
    mvLT = puNeighbour->getMotionInfo(posLB).mv[refPicList];
    mvRT = puNeighbour->getMotionInfo(posRB).mv[refPicList];
    posNeiY += neiH;
    isTopCtuBoundary = true;
  }

  int shift = MAX_CU_DEPTH;
  int iDMvHorX, iDMvHorY, iDMvVerX, iDMvVerY;

  iDMvHorX = (mvRT - mvLT).hor << (shift - Log2(neiW));
  iDMvHorY = (mvRT - mvLT).ver << (shift - Log2(neiW));
  if (puNeighbour->cu->affineType == AFFINEMODEL_6PARAM && !isTopCtuBoundary)
  {
    iDMvVerX = (mvLB - mvLT).hor << (shift - Log2(neiH));
    iDMvVerY = (mvLB - mvLT).ver << (shift - Log2(neiH));
  }
  else
  {
    iDMvVerX = -iDMvHorY;
    iDMvVerY = iDMvHorX;
  }

  int iMvScaleHor = mvLT.hor << shift;
  int iMvScaleVer = mvLT.ver << shift;
  int horTmp, verTmp;

  // v0
  horTmp = iMvScaleHor + iDMvHorX * (posCurX - posNeiX) + iDMvVerX * (posCurY - posNeiY);
  verTmp = iMvScaleVer + iDMvHorY * (posCurX - posNeiX) + iDMvVerY * (posCurY - posNeiY);
  roundAffineMv(horTmp, verTmp, shift);
  rcMv[0].hor = horTmp;
  rcMv[0].ver = verTmp;
  rcMv[0].clipToStorageBitDepth();

  // v1
  horTmp = iMvScaleHor + iDMvHorX * (posCurX + curW - posNeiX) + iDMvVerX * (posCurY - posNeiY);
  verTmp = iMvScaleVer + iDMvHorY * (posCurX + curW - posNeiX) + iDMvVerY * (posCurY - posNeiY);
  roundAffineMv(horTmp, verTmp, shift);
  rcMv[1].hor = horTmp;
  rcMv[1].ver = verTmp;
  rcMv[1].clipToStorageBitDepth();

  // v2
  if (pu.cu->affineType == AFFINEMODEL_6PARAM)
  {
    horTmp = iMvScaleHor + iDMvHorX * (posCurX - posNeiX) + iDMvVerX * (posCurY + curH - posNeiY);
    verTmp = iMvScaleVer + iDMvHorY * (posCurX - posNeiX) + iDMvVerY * (posCurY + curH - posNeiY);
    roundAffineMv(horTmp, verTmp, shift);
    rcMv[2].hor = horTmp;
    rcMv[2].ver = verTmp;
    rcMv[2].clipToStorageBitDepth();
  }
}

void PU::fillAffineMvpCand(PredictionUnit &pu, const RefPicList refPicList, const int refIdx, AffineAMVPInfo &affiAMVPInfo)
{
  affiAMVPInfo.numCand = 0;

  if (refIdx < 0)
  {
    return;
  }

  // insert inherited affine candidates
  Mv outputAffineMv[3];
  Position posLT = pu.Y().topLeft();
  Position posRT = pu.Y().topRight();
  Position posLB = pu.Y().bottomLeft();

  // check left neighbor
  if (!addAffineMVPCandUnscaled(pu, refPicList, refIdx, posLB, MD_BELOW_LEFT, affiAMVPInfo))
  {
    addAffineMVPCandUnscaled(pu, refPicList, refIdx, posLB, MD_LEFT, affiAMVPInfo);
  }

  // check above neighbor
  if (!addAffineMVPCandUnscaled(pu, refPicList, refIdx, posRT, MD_ABOVE_RIGHT, affiAMVPInfo))
  {
    if (!addAffineMVPCandUnscaled(pu, refPicList, refIdx, posRT, MD_ABOVE, affiAMVPInfo))
    {
      addAffineMVPCandUnscaled(pu, refPicList, refIdx, posLT, MD_ABOVE_LEFT, affiAMVPInfo);
    }
  }

  if (affiAMVPInfo.numCand >= AMVP_MAX_NUM_CANDS)
  {
    for (int i = 0; i < affiAMVPInfo.numCand; i++)
    {
      affiAMVPInfo.mvCandLT[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
      affiAMVPInfo.mvCandRT[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
      affiAMVPInfo.mvCandLB[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
    }
    return;
  }

  // insert constructed affine candidates
  int cornerMVPattern = 0;

  //-------------------  V0 (START) -------------------//
  AMVPInfo amvpInfo0;
  amvpInfo0.numCand = 0;

  // A->C: Above Left, Above, Left
  addMVPCandUnscaled(pu, refPicList, refIdx, posLT, MD_ABOVE_LEFT, amvpInfo0);
  if (amvpInfo0.numCand < 1)
  {
    addMVPCandUnscaled(pu, refPicList, refIdx, posLT, MD_ABOVE, amvpInfo0);
  }
  if (amvpInfo0.numCand < 1)
  {
    addMVPCandUnscaled(pu, refPicList, refIdx, posLT, MD_LEFT, amvpInfo0);
  }
  cornerMVPattern = cornerMVPattern | amvpInfo0.numCand;

  //-------------------  V1 (START) -------------------//
  AMVPInfo amvpInfo1;
  amvpInfo1.numCand = 0;

  // D->E: Above, Above Right
  addMVPCandUnscaled(pu, refPicList, refIdx, posRT, MD_ABOVE, amvpInfo1);
  if (amvpInfo1.numCand < 1)
  {
    addMVPCandUnscaled(pu, refPicList, refIdx, posRT, MD_ABOVE_RIGHT, amvpInfo1);
  }
  cornerMVPattern = cornerMVPattern | (amvpInfo1.numCand << 1);

  //-------------------  V2 (START) -------------------//
  AMVPInfo amvpInfo2;
  amvpInfo2.numCand = 0;

  // F->G: Left, Below Left
  addMVPCandUnscaled(pu, refPicList, refIdx, posLB, MD_LEFT, amvpInfo2);
  if (amvpInfo2.numCand < 1)
  {
    addMVPCandUnscaled(pu, refPicList, refIdx, posLB, MD_BELOW_LEFT, amvpInfo2);
  }
  cornerMVPattern = cornerMVPattern | (amvpInfo2.numCand << 2);

  outputAffineMv[0] = amvpInfo0.mvCand[0];
  outputAffineMv[1] = amvpInfo1.mvCand[0];
  outputAffineMv[2] = amvpInfo2.mvCand[0];

  outputAffineMv[0].roundAffinePrecInternal2Amvr(pu.cu->imv);
  outputAffineMv[1].roundAffinePrecInternal2Amvr(pu.cu->imv);
  outputAffineMv[2].roundAffinePrecInternal2Amvr(pu.cu->imv);

  if (cornerMVPattern == 7 || (cornerMVPattern == 3 && pu.cu->affineType == AFFINEMODEL_4PARAM))
  {
    affiAMVPInfo.mvCandLT[affiAMVPInfo.numCand] = outputAffineMv[0];
    affiAMVPInfo.mvCandRT[affiAMVPInfo.numCand] = outputAffineMv[1];
    affiAMVPInfo.mvCandLB[affiAMVPInfo.numCand] = outputAffineMv[2];
    affiAMVPInfo.numCand++;
  }

  if (affiAMVPInfo.numCand < 2)
  {
    // check corner MVs
    for (int i = 2; i >= 0 && affiAMVPInfo.numCand < AMVP_MAX_NUM_CANDS; i--)
    {
      if (cornerMVPattern & (1 << i)) // MV i exist
      {
        affiAMVPInfo.mvCandLT[affiAMVPInfo.numCand] = outputAffineMv[i];
        affiAMVPInfo.mvCandRT[affiAMVPInfo.numCand] = outputAffineMv[i];
        affiAMVPInfo.mvCandLB[affiAMVPInfo.numCand] = outputAffineMv[i];
        affiAMVPInfo.numCand++;
      }
    }

    // Get Temporal Motion Predictor
    if (affiAMVPInfo.numCand < 2 && pu.cs->picHeader->enableTMVP)
    {
      const int refIdxCol = refIdx;

      Position posRB = pu.Y().bottomRight().offset(-3, -3);

      const PreCalcValues& pcv = *pu.cs->pcv;

      Position posC0;
      bool C0Avail = false;
      Position posC1 = pu.Y().center();
      Mv cColMv;
      bool boundaryCond = ((posRB.x + pcv.minCUSize) < pcv.lumaWidth) && ((posRB.y + pcv.minCUSize) < pcv.lumaHeight);
      SubPic curSubPic = pu.cs->slice->pps->getSubPicFromPos(pu.lumaPos());
      if (curSubPic.treatedAsPic)
      {
        boundaryCond = ((posRB.x + pcv.minCUSize) <= curSubPic.subPicRight &&
          (posRB.y + pcv.minCUSize) <= curSubPic.subPicBottom);
      }
      if (boundaryCond)
      {
        int posYInCtu = posRB.y & pcv.maxCUSizeMask;
        if (posYInCtu + 4 < pcv.maxCUSize)
        {
          posC0 = posRB.offset(4, 4);
          C0Avail = true;
        }
      }
      if ((C0Avail && getColocatedMVP(pu, refPicList, posC0, cColMv, refIdxCol)) || getColocatedMVP(pu, refPicList, posC1, cColMv, refIdxCol))
      {
        cColMv.roundAffinePrecInternal2Amvr(pu.cu->imv);
        affiAMVPInfo.mvCandLT[affiAMVPInfo.numCand] = cColMv;
        affiAMVPInfo.mvCandRT[affiAMVPInfo.numCand] = cColMv;
        affiAMVPInfo.mvCandLB[affiAMVPInfo.numCand] = cColMv;
        affiAMVPInfo.numCand++;
      }
    }

    if (affiAMVPInfo.numCand < 2)
    {
      // add zero MV
      for (int i = affiAMVPInfo.numCand; i < AMVP_MAX_NUM_CANDS; i++)
      {
        affiAMVPInfo.mvCandLT[affiAMVPInfo.numCand].setZero();
        affiAMVPInfo.mvCandRT[affiAMVPInfo.numCand].setZero();
        affiAMVPInfo.mvCandLB[affiAMVPInfo.numCand].setZero();
        affiAMVPInfo.numCand++;
      }
    }
  }

  for (int i = 0; i < affiAMVPInfo.numCand; i++)
  {
    affiAMVPInfo.mvCandLT[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
    affiAMVPInfo.mvCandRT[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
    affiAMVPInfo.mvCandLB[i].roundAffinePrecInternal2Amvr(pu.cu->imv);
  }
}

bool PU::addMVPCandUnscaled( const PredictionUnit &pu, const RefPicList refPicList, const int iRefIdx, const Position& pos, const MvpDir dir, AMVPInfo &info )
{
        CodingStructure &cs    = *pu.cs;
  const PredictionUnit *neibPU = NULL;
        Position neibPos;

  switch (dir)
  {
  case MD_LEFT:
    neibPos = pos.offset( -1,  0 );
    break;
  case MD_ABOVE:
    neibPos = pos.offset(  0, -1 );
    break;
  case MD_ABOVE_RIGHT:
    neibPos = pos.offset(  1, -1 );
    break;
  case MD_BELOW_LEFT:
    neibPos = pos.offset( -1,  1 );
    break;
  case MD_ABOVE_LEFT:
    neibPos = pos.offset( -1, -1 );
    break;
  default:
    break;
  }

  neibPU = cs.getPURestricted( neibPos, pu, pu.chType );

  if( neibPU == NULL || !CU::isInter( *neibPU->cu ) )
  {
    return false;
  }

  const MotionInfo& neibMi        = neibPU->getMotionInfo( neibPos );

  const int        currRefPOC     = cs.slice->getRefPic( refPicList, iRefIdx )->getPOC();
  const RefPicList refPicList2nd = ( refPicList == REF_PIC_LIST_0 ) ? REF_PIC_LIST_1 : REF_PIC_LIST_0;

  for( int predictorSource = 0; predictorSource < 2; predictorSource++ ) // examine the indicated reference picture list, then if not available, examine the other list.
  {
    const RefPicList refPicListIndex = ( predictorSource == 0 ) ? refPicList : refPicList2nd;
    const int        neibRefIdx       = neibMi.refIdx[refPicListIndex];

    if( neibRefIdx >= 0 && currRefPOC == cs.slice->getRefPOC( refPicListIndex, neibRefIdx ) )
    {
      info.mvCand[info.numCand++] = neibMi.mv[refPicListIndex];
      return true;
    }
  }

  return false;
}


void PU::addAMVPHMVPCand(const PredictionUnit &pu, const RefPicList refPicList, const int currRefPOC, AMVPInfo &info)
{
  const Slice &slice = *(*pu.cs).slice;

  auto &lut = /*CU::isIBC(*pu.cu) ? pu.cs->motionLut.lutIbc :*/ pu.cs->motionLut.lut;
  int num_avai_candInLUT = (int) lut.size();
  int num_allowedCand = std::min(MAX_NUM_HMVP_AVMPCANDS, num_avai_candInLUT);
  const RefPicList refPicList2nd = (refPicList == REF_PIC_LIST_0) ? REF_PIC_LIST_1 : REF_PIC_LIST_0;

  for (int mrgIdx = 1; mrgIdx <= num_allowedCand; mrgIdx++)
  {
    if (info.numCand >= AMVP_MAX_NUM_CANDS)
    {
      return;
    }
    const HPMVInfo& neibMi = lut[mrgIdx - 1];

    for (int predictorSource = 0; predictorSource < 2; predictorSource++)
    {
      const RefPicList refPicListIndex = (predictorSource == 0) ? refPicList : refPicList2nd;
      const int        neibRefIdx = neibMi.refIdx[refPicListIndex];

      if (neibRefIdx >= 0 && (CU::isIBC(*pu.cu) || (currRefPOC == slice.getRefPOC(refPicListIndex, neibRefIdx))))
      {
        Mv pmv = neibMi.mv[refPicListIndex];
        pmv.roundTransPrecInternal2Amvr(pu.cu->imv);

        info.mvCand[info.numCand++] = pmv;
        if (info.numCand >= AMVP_MAX_NUM_CANDS)
        {
          return;
        }
      }
    }
  }
}

bool PU::isBipredRestriction(const PredictionUnit &pu)
{
  if(pu.cu->lumaSize().width == 4 && pu.cu->lumaSize().height ==4 )
  {
    return true;
  }
  /* disable bi-prediction for 4x8/8x4 */
  if ( pu.cu->lumaSize().width + pu.cu->lumaSize().height == 12 )
  {
    return true;
  }
  return false;
}

void PU::getAffineControlPointCand(const PredictionUnit &pu, MotionInfo mi[4], bool isAvailable[4], int verIdx[4], int8_t BcwIdx, int modelIdx, int verNum, AffineMergeCtx& affMrgType)
{
  int cuW = pu.Y().width;
  int cuH = pu.Y().height;
  int vx, vy;
  int shift = MAX_CU_DEPTH;
  int shiftHtoW = shift + Log2(cuW) - Log2(cuH);

  // motion info
  Mv cMv[2][4];
  int refIdx[2] = { -1, -1 };
  int dir = 0;
  EAffineModel curType = (verNum == 2) ? AFFINEMODEL_4PARAM : AFFINEMODEL_6PARAM;

  if (verNum == 2)
  {
    int idx0 = verIdx[0], idx1 = verIdx[1];
    if (!isAvailable[idx0] || !isAvailable[idx1])
    {
      return;
    }

    for (int l = 0; l < 2; l++)
    {
      if (mi[idx0].refIdx[l] >= 0 && mi[idx1].refIdx[l] >= 0)
      {
        // check same refidx and different mv
        if (mi[idx0].refIdx[l] == mi[idx1].refIdx[l])
        {
          dir |= (l + 1);
          refIdx[l] = mi[idx0].refIdx[l];
        }
      }
    }
  }
  else if (verNum == 3)
  {
    int idx0 = verIdx[0], idx1 = verIdx[1], idx2 = verIdx[2];
    if (!isAvailable[idx0] || !isAvailable[idx1] || !isAvailable[idx2])
    {
      return;
    }

    for (int l = 0; l < 2; l++)
    {
      if (mi[idx0].refIdx[l] >= 0 && mi[idx1].refIdx[l] >= 0 && mi[idx2].refIdx[l] >= 0)
      {
        // check same refidx and different mv
        if (mi[idx0].refIdx[l] == mi[idx1].refIdx[l] && mi[idx0].refIdx[l] == mi[idx2].refIdx[l])
        {
          dir |= (l + 1);
          refIdx[l] = mi[idx0].refIdx[l];
        }
      }
      }
    }

  if (dir == 0)
  {
    return;
  }


  for (int l = 0; l < 2; l++)
  {
    if (dir & (l + 1))
    {
      for (int i = 0; i < verNum; i++)
      {
        cMv[l][verIdx[i]] = mi[verIdx[i]].mv[l];
      }

      // convert to LT, RT[, [LB]]
      switch (modelIdx)
      {
      case 0: // 0 : LT, RT, LB
        break;

      case 1: // 1 : LT, RT, RB
        cMv[l][2].hor = cMv[l][3].hor + cMv[l][0].hor - cMv[l][1].hor;
        cMv[l][2].ver = cMv[l][3].ver + cMv[l][0].ver - cMv[l][1].ver;
        cMv[l][2].clipToStorageBitDepth();
        break;

      case 2: // 2 : LT, LB, RB
        cMv[l][1].hor = cMv[l][3].hor + cMv[l][0].hor - cMv[l][2].hor;
        cMv[l][1].ver = cMv[l][3].ver + cMv[l][0].ver - cMv[l][2].ver;
        cMv[l][1].clipToStorageBitDepth();
        break;

      case 3: // 3 : RT, LB, RB
        cMv[l][0].hor = cMv[l][1].hor + cMv[l][2].hor - cMv[l][3].hor;
        cMv[l][0].ver = cMv[l][1].ver + cMv[l][2].ver - cMv[l][3].ver;
        cMv[l][0].clipToStorageBitDepth();
        break;

      case 4: // 4 : LT, RT
        break;

      case 5: // 5 : LT, LB
        vx = (cMv[l][0].hor << shift) + ((cMv[l][2].ver - cMv[l][0].ver) << shiftHtoW);
        vy = (cMv[l][0].ver << shift) - ((cMv[l][2].hor - cMv[l][0].hor) << shiftHtoW);
        roundAffineMv(vx, vy, shift);
        cMv[l][1].set(vx, vy);
        cMv[l][1].clipToStorageBitDepth();
        break;

      default:
        CHECK(1, "Invalid model index!\n");
        break;
      }
    }
    else
    {
      for (int i = 0; i < 4; i++)
      {
        cMv[l][i].hor = 0;
        cMv[l][i].ver = 0;
      }
    }
  }

  for (int i = 0; i < 3; i++)
  {
    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 0][i].mv = cMv[0][i];
    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 0][i].refIdx = refIdx[0];

    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 1][i].mv = cMv[1][i];
    affMrgType.mvFieldNeighbours[(affMrgType.numValidMergeCand << 1) + 1][i].refIdx = refIdx[1];
  }
  affMrgType.interDirNeighbours[affMrgType.numValidMergeCand] = dir;
  affMrgType.affineType[affMrgType.numValidMergeCand] = curType;
  affMrgType.BcwIdx[affMrgType.numValidMergeCand] = (dir == 3) ? BcwIdx : BCW_DEFAULT;
  affMrgType.numValidMergeCand++;


  return;
}


bool PU::getInterMergeSbTMVPCand(const PredictionUnit &pu, MergeCtx& mrgCtx, bool& LICFlag, const int count, int mmvdList)
{
  const Slice   &slice = *pu.cs->slice;
  const unsigned scale = 4 * std::max<int>(1, 4 * AMVP_DECIMATION_FACTOR / 4);
  const unsigned mask = ~(scale - 1);

  const Picture *pColPic = slice.getRefPic(RefPicList(slice.isInterB() ? 1 - slice.colFromL0Flag : 0), slice.colRefIdx);
  Mv cTMv;

  if (count)
  {
    if ((mrgCtx.interDirNeighbours[0] & (1 << REF_PIC_LIST_0)) && slice.getRefPic(REF_PIC_LIST_0, mrgCtx.mvFieldNeighbours[REF_PIC_LIST_0].refIdx) == pColPic)
    {
      cTMv = mrgCtx.mvFieldNeighbours[REF_PIC_LIST_0].mv;
    }
    else if (slice.isInterB() && (mrgCtx.interDirNeighbours[0] & (1 << REF_PIC_LIST_1)) && slice.getRefPic(REF_PIC_LIST_1, mrgCtx.mvFieldNeighbours[REF_PIC_LIST_1].refIdx) == pColPic)
    {
      cTMv = mrgCtx.mvFieldNeighbours[REF_PIC_LIST_1].mv;
    }
  }

  ///////////////////////////////////////////////////////////////////////
  ////////          GET Initial Temporal Vector                  ////////
  ///////////////////////////////////////////////////////////////////////
  Mv cTempVector = cTMv;
  bool  tempLICFlag = false;

  // compute the location of the current PU
  Position puPos = pu.lumaPos();
  Size puSize = pu.lumaSize();
  int numPartLine = std::max(puSize.width >> ATMVP_SUB_BLOCK_SIZE, 1u);
  int numPartCol = std::max(puSize.height >> ATMVP_SUB_BLOCK_SIZE, 1u);
  int puHeight = numPartCol == 1 ? puSize.height : 1 << ATMVP_SUB_BLOCK_SIZE;
  int puWidth = numPartLine == 1 ? puSize.width : 1 << ATMVP_SUB_BLOCK_SIZE;

  Mv cColMv;
  int refIdx = 0;
  // use coldir.
  bool     bBSlice = slice.isInterB();

  Position centerPos;

  bool found = false;
  cTempVector = cTMv;

  cTempVector.changePrecision(MV_PRECISION_SIXTEENTH, MV_PRECISION_INT);
  int tempX = cTempVector.hor;
  int tempY = cTempVector.ver;

  centerPos.x = puPos.x + (puSize.width >> 1) + tempX;
  centerPos.y = puPos.y + (puSize.height >> 1) + tempY;

  clipColPos(centerPos.x, centerPos.y, pu);

  centerPos = Position{ PosType(centerPos.x & mask), PosType(centerPos.y & mask) };

  // derivation of center motion parameters from the collocated CU
  const MotionInfo &mi = pColPic->cs->getMotionInfo(centerPos);

  if (mi.isInter)
  {
    mrgCtx.interDirNeighbours[count] = 0;

    for (unsigned currRefListId = 0; currRefListId < (bBSlice ? 2 : 1); currRefListId++)
    {
      RefPicList  currRefPicList = RefPicList(currRefListId);

      if (getColocatedMVP(pu, currRefPicList, centerPos, cColMv, refIdx, true))
      {
        // set as default, for further motion vector field spanning
        mrgCtx.mvFieldNeighbours[(count << 1) + currRefListId].setMvField(cColMv, 0);
        mrgCtx.interDirNeighbours[count] |= (1 << currRefListId);
        LICFlag = tempLICFlag;
        mrgCtx.BcwIdx[count] = BCW_DEFAULT;
        found = true;
      }
      else
      {
        mrgCtx.mvFieldNeighbours[(count << 1) + currRefListId].setMvField(Mv(), NOT_VALID);
        mrgCtx.interDirNeighbours[count] &= ~(1 << currRefListId);
      }
    }
  }

  if (!found)
  {
    return false;
  }
  if (mmvdList != 1)
  {
    int xOff = (puWidth >> 1) + tempX;
    int yOff = (puHeight >> 1) + tempY;

    MotionBuf& mb = mrgCtx.subPuMvpMiBuf;

    const bool isBiPred = isBipredRestriction(pu);

    for (int y = puPos.y; y < puPos.y + puSize.height; y += puHeight)
    {
      for (int x = puPos.x; x < puPos.x + puSize.width; x += puWidth)
      {
        Position colPos{ x + xOff, y + yOff };

        clipColPos(colPos.x, colPos.y, pu);

        colPos = Position{ PosType(colPos.x & mask), PosType(colPos.y & mask) };

        const MotionInfo &colMi = pColPic->cs->getMotionInfo(colPos);

        MotionInfo mi;

        found = false;
        mi.isInter = true;
        mi.sliceIdx = slice.independentSliceIdx;
        if (colMi.isInter)
        {
          for (unsigned currRefListId = 0; currRefListId < (bBSlice ? 2 : 1); currRefListId++)
          {
            RefPicList currRefPicList = RefPicList(currRefListId);
            if (getColocatedMVP(pu, currRefPicList, colPos, cColMv, refIdx, true))
            {
              mi.refIdx[currRefListId] = 0;
              mi.mv[currRefListId] = cColMv;
              found = true;
            }
          }
        }
        if (!found)
        {
          mi.mv[0] = mrgCtx.mvFieldNeighbours[(count << 1) + 0].mv;
          mi.mv[1] = mrgCtx.mvFieldNeighbours[(count << 1) + 1].mv;
          mi.refIdx[0] = mrgCtx.mvFieldNeighbours[(count << 1) + 0].refIdx;
          mi.refIdx[1] = mrgCtx.mvFieldNeighbours[(count << 1) + 1].refIdx;
        }

        mi.interDir = (mi.refIdx[0] != -1 ? 1 : 0) + (mi.refIdx[1] != -1 ? 2 : 0);

        if (isBiPred && mi.interDir == 3)
        {
          mi.interDir = 1;
          mi.mv[1] = Mv();
          mi.refIdx[1] = NOT_VALID;
        }

        mb.subBuf(g_miScaling.scale(Position{ x, y } -pu.lumaPos()), g_miScaling.scale(Size(puWidth, puHeight))).fill(mi);
      }
    }
  }
  return true;
}


const int getAvailableAffineNeighboursForLeftPredictor(const PredictionUnit &pu, const PredictionUnit* npu[])
{
  const Position posLB = pu.Y().bottomLeft();
  int num = 0;
  const unsigned plevel = pu.cs->sps->log2ParallelMergeLevelMinus2 + 2;

  const PredictionUnit *puLeftBottom = pu.cs->getPURestricted(posLB.offset(-1, 1), pu, pu.chType);
  if (puLeftBottom && puLeftBottom->cu->affine
    && puLeftBottom->mergeType == MRG_TYPE_DEFAULT_N
    && PU::isDiffMER(pu.lumaPos(), posLB.offset(-1, 1), plevel)
    )
  {
    npu[num++] = puLeftBottom;
    return num;
  }

  const PredictionUnit* puLeft = pu.cs->getPURestricted(posLB.offset(-1, 0), pu, pu.chType);
  if (puLeft && puLeft->cu->affine
    && puLeft->mergeType == MRG_TYPE_DEFAULT_N
    && PU::isDiffMER(pu.lumaPos(), posLB.offset(-1, 0), plevel)
    )
  {
    npu[num++] = puLeft;
    return num;
  }

  return num;
}

const int getAvailableAffineNeighboursForAbovePredictor(const PredictionUnit &pu, const PredictionUnit* npu[], int numAffNeighLeft)
{
  const Position posLT = pu.Y().topLeft();
  const Position posRT = pu.Y().topRight();
  const unsigned plevel = pu.cs->sps->log2ParallelMergeLevelMinus2 + 2;
  int num = numAffNeighLeft;

  const PredictionUnit* puAboveRight = pu.cs->getPURestricted(posRT.offset(1, -1), pu, pu.chType);
  if (puAboveRight && puAboveRight->cu->affine
    && puAboveRight->mergeType == MRG_TYPE_DEFAULT_N
    && PU::isDiffMER(pu.lumaPos(), posRT.offset(1, -1), plevel)
    )
  {
    npu[num++] = puAboveRight;
    return num;
  }

  const PredictionUnit* puAbove = pu.cs->getPURestricted(posRT.offset(0, -1), pu, pu.chType);
  if (puAbove && puAbove->cu->affine
    && puAbove->mergeType == MRG_TYPE_DEFAULT_N
    && PU::isDiffMER(pu.lumaPos(), posRT.offset(0, -1), plevel)
    )
  {
    npu[num++] = puAbove;
    return num;
  }

  const PredictionUnit *puAboveLeft = pu.cs->getPURestricted(posLT.offset(-1, -1), pu, pu.chType);
  if (puAboveLeft && puAboveLeft->cu->affine
    && puAboveLeft->mergeType == MRG_TYPE_DEFAULT_N
    && PU::isDiffMER(pu.lumaPos(), posLT.offset(-1, -1), plevel)
    )
  {
    npu[num++] = puAboveLeft;
    return num;
  }

  return num;
}

void PU::getAffineMergeCand(const PredictionUnit &pu, AffineMergeCtx& affMrgCtx, const int mrgCandIdx)
{
  const CodingStructure &cs = *pu.cs;
  const Slice &slice = *pu.cs->slice;
  const uint32_t maxNumAffineMergeCand = slice.picHeader->maxNumAffineMergeCand;
  const unsigned plevel = pu.cs->sps->log2ParallelMergeLevelMinus2 + 2;

  for (int i = 0; i < maxNumAffineMergeCand; i++)
  {
    for (int mvNum = 0; mvNum < 3; mvNum++)
    {
      affMrgCtx.mvFieldNeighbours[(i << 1) + 0][mvNum].setMvField(Mv(), -1);
      affMrgCtx.mvFieldNeighbours[(i << 1) + 1][mvNum].setMvField(Mv(), -1);
    }
    affMrgCtx.interDirNeighbours[i] = 0;
    affMrgCtx.affineType[i] = AFFINEMODEL_4PARAM;
    affMrgCtx.mergeType[i] = MRG_TYPE_DEFAULT_N;
    affMrgCtx.BcwIdx[i] = BCW_DEFAULT;
  }

  affMrgCtx.numValidMergeCand = 0;
  affMrgCtx.maxNumMergeCand = maxNumAffineMergeCand;
  bool enableSbTMVP = slice.sps->SbtMvp && !(slice.poc == slice.getRefPic(REF_PIC_LIST_0, 0)->getPOC() && slice.isIRAP());
  bool isAvailableSubPu = false;

  if (enableSbTMVP && slice.picHeader->enableTMVP)
  {
    MergeCtx mrgCtx = *affMrgCtx.mrgCtx;
    bool tmpLICFlag = false;
    CHECK(mrgCtx.subPuMvpMiBuf.area() == 0 || !mrgCtx.subPuMvpMiBuf.buf, "Buffer not initialized");
    mrgCtx.subPuMvpMiBuf.fill(MotionInfo());

    int pos = 0;
    // Get spatial MV
    const Position posCurLB = pu.Y().bottomLeft();
    MotionInfo miLeft;

    //left
    const PredictionUnit* puLeft = cs.getPURestricted(posCurLB.offset(-1, 0), pu, pu.chType);
    const bool isAvailableA1 = puLeft && isDiffMER(pu.lumaPos(), posCurLB.offset(-1, 0), plevel) && pu.cu != puLeft->cu && CU::isInter(*puLeft->cu);
    if (isAvailableA1)
    {
      miLeft = puLeft->getMotionInfo(posCurLB.offset(-1, 0));
      // get Inter Dir
      mrgCtx.interDirNeighbours[pos] = miLeft.interDir;

      // get Mv from Left
      mrgCtx.mvFieldNeighbours[pos << 1].setMvField(miLeft.mv[0], miLeft.refIdx[0]);

      if (slice.isInterB())
      {
        mrgCtx.mvFieldNeighbours[(pos << 1) + 1].setMvField(miLeft.mv[1], miLeft.refIdx[1]);
      }
      pos++;
    }

    mrgCtx.numValidMergeCand = pos;
    isAvailableSubPu = getInterMergeSbTMVPCand(pu, mrgCtx, tmpLICFlag, pos, 0);
    if (isAvailableSubPu)
    {
      for (int mvNum = 0; mvNum < 3; mvNum++)
      {
        affMrgCtx.mvFieldNeighbours[(affMrgCtx.numValidMergeCand << 1) + 0][mvNum].setMvField(mrgCtx.mvFieldNeighbours[(pos << 1) + 0].mv, mrgCtx.mvFieldNeighbours[(pos << 1) + 0].refIdx);
        affMrgCtx.mvFieldNeighbours[(affMrgCtx.numValidMergeCand << 1) + 1][mvNum].setMvField(mrgCtx.mvFieldNeighbours[(pos << 1) + 1].mv, mrgCtx.mvFieldNeighbours[(pos << 1) + 1].refIdx);
      }
      affMrgCtx.interDirNeighbours[affMrgCtx.numValidMergeCand] = mrgCtx.interDirNeighbours[pos];

      affMrgCtx.affineType[affMrgCtx.numValidMergeCand] = AFFINE_MODEL_NUM;
      affMrgCtx.mergeType[affMrgCtx.numValidMergeCand] = MRG_TYPE_SUBPU_ATMVP;
      if (affMrgCtx.numValidMergeCand == mrgCandIdx)
      {
        return;
      }

      affMrgCtx.numValidMergeCand++;

      // early termination
      if (affMrgCtx.numValidMergeCand == maxNumAffineMergeCand)
      {
        return;
      }
    }
  }

  if (slice.sps->Affine)
  {
    ///> Start: inherited affine candidates
    const PredictionUnit* npu[5];
    int numAffNeighLeft = getAvailableAffineNeighboursForLeftPredictor(pu, npu);
    int numAffNeigh = getAvailableAffineNeighboursForAbovePredictor(pu, npu, numAffNeighLeft);
    for (int idx = 0; idx < numAffNeigh; idx++)
    {
      // derive Mv from Neigh affine PU
      Mv cMv[2][3];
      const PredictionUnit* puNeigh = npu[idx];
      pu.cu->affineType = puNeigh->cu->affineType;
      if (puNeigh->interDir != 2)
      {
        xInheritedAffineMv(pu, puNeigh, REF_PIC_LIST_0, cMv[0]);
      }
      if (slice.isInterB())
      {
        if (puNeigh->interDir != 1)
        {
          xInheritedAffineMv(pu, puNeigh, REF_PIC_LIST_1, cMv[1]);
        }
      }

      for (int mvNum = 0; mvNum < 3; mvNum++)
      {
        affMrgCtx.mvFieldNeighbours[(affMrgCtx.numValidMergeCand << 1) + 0][mvNum].setMvField(cMv[0][mvNum], puNeigh->refIdx[0]);
        affMrgCtx.mvFieldNeighbours[(affMrgCtx.numValidMergeCand << 1) + 1][mvNum].setMvField(cMv[1][mvNum], puNeigh->refIdx[1]);
      }
      affMrgCtx.interDirNeighbours[affMrgCtx.numValidMergeCand] = puNeigh->interDir;
      affMrgCtx.affineType[affMrgCtx.numValidMergeCand] = (EAffineModel)(puNeigh->cu->affineType);
      affMrgCtx.BcwIdx[affMrgCtx.numValidMergeCand] = puNeigh->cu->BcwIdx;

      if (affMrgCtx.numValidMergeCand == mrgCandIdx)
      {
        return;
      }

      // early termination
      affMrgCtx.numValidMergeCand++;
      if (affMrgCtx.numValidMergeCand == maxNumAffineMergeCand)
      {
        return;
      }
    }
    ///> End: inherited affine candidates

    ///> Start: Constructed affine candidates
    {
      MotionInfo mi[4];
      bool isAvailable[4] = { false };

      int8_t neighBcw[2] = { BCW_DEFAULT, BCW_DEFAULT };
      // control point: LT B2->B3->A2
      const Position posLT[3] = { pu.Y().topLeft().offset(-1, -1), pu.Y().topLeft().offset(0, -1), pu.Y().topLeft().offset(-1, 0) };
      for (int i = 0; i < 3; i++)
      {
        const Position pos = posLT[i];
        const PredictionUnit* puNeigh = cs.getPURestricted(pos, pu, pu.chType);

        if (puNeigh && CU::isInter(*puNeigh->cu)
          && PU::isDiffMER(pu.lumaPos(), pos, plevel)
          )
        {
          isAvailable[0] = true;
          mi[0] = puNeigh->getMotionInfo(pos);
          neighBcw[0] = puNeigh->cu->BcwIdx;
          break;
        }
      }

      // control point: RT B1->B0
      const Position posRT[2] = { pu.Y().topRight().offset(0, -1), pu.Y().topRight().offset(1, -1) };
      for (int i = 0; i < 2; i++)
      {
        const Position pos = posRT[i];
        const PredictionUnit* puNeigh = cs.getPURestricted(pos, pu, pu.chType);


        if (puNeigh && CU::isInter(*puNeigh->cu)
          && PU::isDiffMER(pu.lumaPos(), pos, plevel)
          )
        {
          isAvailable[1] = true;
          mi[1] = puNeigh->getMotionInfo(pos);
          neighBcw[1] = puNeigh->cu->BcwIdx;
          break;
        }
      }

      // control point: LB A1->A0
      const Position posLB[2] = { pu.Y().bottomLeft().offset(-1, 0), pu.Y().bottomLeft().offset(-1, 1) };
      for (int i = 0; i < 2; i++)
      {
        const Position pos = posLB[i];
        const PredictionUnit* puNeigh = cs.getPURestricted(pos, pu, pu.chType);


        if (puNeigh && CU::isInter(*puNeigh->cu)
          && PU::isDiffMER(pu.lumaPos(), pos, plevel)
          )
        {
          isAvailable[2] = true;
          mi[2] = puNeigh->getMotionInfo(pos);
          break;
        }
      }

      // control point: RB
      if (slice.picHeader->enableTMVP)
      {
        //>> MTK colocated-RightBottom
        // offset the pos to be sure to "point" to the same position the uiAbsPartIdx would've pointed to
        Position posRB = pu.Y().bottomRight().offset(-3, -3);

        const PreCalcValues& pcv = *cs.pcv;
        Position posC0;
        bool C0Avail = false;

        bool boundaryCond = ((posRB.x + pcv.minCUSize) < pcv.lumaWidth) && ((posRB.y + pcv.minCUSize) < pcv.lumaHeight);
        SubPic curSubPic = pu.cs->slice->pps->getSubPicFromPos(pu.lumaPos());
        if (curSubPic.treatedAsPic)
        {
          boundaryCond = ((posRB.x + pcv.minCUSize) <= curSubPic.subPicRight &&
            (posRB.y + pcv.minCUSize) <= curSubPic.subPicBottom);
        }
        if (boundaryCond)
        {
          int posYInCtu = posRB.y & pcv.maxCUSizeMask;
          if (posYInCtu + 4 < pcv.maxCUSize)
          {
            posC0 = posRB.offset(4, 4);
            C0Avail = true;
          }
        }

        Mv        cColMv;
        int       refIdx = 0;
        bool      bExistMV = C0Avail && getColocatedMVP(pu, REF_PIC_LIST_0, posC0, cColMv, refIdx);
        if (bExistMV)
        {
          mi[3].mv[0] = cColMv;
          mi[3].refIdx[0] = refIdx;
          mi[3].interDir = 1;
          isAvailable[3] = true;
        }

        if (slice.isInterB())
        {
          bExistMV = C0Avail && getColocatedMVP(pu, REF_PIC_LIST_1, posC0, cColMv, refIdx);
          if (bExistMV)
          {
            mi[3].mv[1] = cColMv;
            mi[3].refIdx[1] = refIdx;
            mi[3].interDir |= 2;
            isAvailable[3] = true;
          }
        }
      }

      //-------------------  insert model  -------------------//
      int order[6] = { 0, 1, 2, 3, 4, 5 };
      int modelNum = 6;
      int model[6][4] = {
        { 0, 1, 2 },          // 0:  LT, RT, LB
        { 0, 1, 3 },          // 1:  LT, RT, RB
        { 0, 2, 3 },          // 2:  LT, LB, RB
        { 1, 2, 3 },          // 3:  RT, LB, RB
        { 0, 1 },             // 4:  LT, RT
        { 0, 2 },             // 5:  LT, LB
      };

      int verNum[6] = { 3, 3, 3, 3, 2, 2 };
      int startIdx = pu.cs->sps->AffineType ? 0 : 4;
      for (int idx = startIdx; idx < modelNum; idx++)
      {
        int modelIdx = order[idx];
        getAffineControlPointCand(pu, mi, isAvailable, model[modelIdx], ((modelIdx == 3) ? neighBcw[1] : neighBcw[0]), modelIdx, verNum[modelIdx], affMrgCtx);
        if (affMrgCtx.numValidMergeCand != 0 && affMrgCtx.numValidMergeCand - 1 == mrgCandIdx)
        {
          return;
        }

        // early termination
        if (affMrgCtx.numValidMergeCand == maxNumAffineMergeCand)
        {
          return;
        }
      }
    }
    ///> End: Constructed affine candidates
  }

  ///> zero padding
  int cnt = affMrgCtx.numValidMergeCand;
  while (cnt < maxNumAffineMergeCand)
  {
    for (int mvNum = 0; mvNum < 3; mvNum++)
    {
      affMrgCtx.mvFieldNeighbours[(cnt << 1) + 0][mvNum].setMvField(Mv(0, 0), 0);
    }
    affMrgCtx.interDirNeighbours[cnt] = 1;

    if (slice.isInterB())
    {
      for (int mvNum = 0; mvNum < 3; mvNum++)
      {
        affMrgCtx.mvFieldNeighbours[(cnt << 1) + 1][mvNum].setMvField(Mv(0, 0), 0);
      }
      affMrgCtx.interDirNeighbours[cnt] = 3;
    }
    affMrgCtx.affineType[cnt] = AFFINEMODEL_4PARAM;

    if (cnt == mrgCandIdx)
    {
      return;
    }
    cnt++;
    affMrgCtx.numValidMergeCand++;
  }
}

void PU::setAllAffineMvField(PredictionUnit &pu, MvField *mvField, RefPicList eRefList)
{
  // Set Mv
  Mv mv[3];
  for (int i = 0; i < 3; i++)
  {
    mv[i] = mvField[i].mv;
  }
  setAllAffineMv(pu, mv[0], mv[1], mv[2], eRefList);

  // Set RefIdx
  CHECK(mvField[0].refIdx != mvField[1].refIdx || mvField[0].refIdx != mvField[2].refIdx, "Affine mv corners don't have the same refIdx.");
  pu.refIdx[eRefList] = mvField[0].refIdx;
}

void PU::setAllAffineMv(PredictionUnit& pu, Mv affLT, Mv affRT, Mv affLB, RefPicList eRefList, bool clipCPMVs)
{
  int width = pu.Y().width;
  int shift = MAX_CU_DEPTH;
  bool SameMV = false;
  if (affLT == affRT)
  {
    if (affRT == affLB)
    {
      SameMV = true;
    }
  }
  if (clipCPMVs)
  {
    affLT.mvCliptoStorageBitDepth();
    affRT.mvCliptoStorageBitDepth();
    if (pu.cu->affineType == AFFINEMODEL_6PARAM)
    {
      affLB.mvCliptoStorageBitDepth();
    }
  }

  int deltaMvHorX = 0;
  int deltaMvHorY = 0;
  int deltaMvVerX = 0;
  int deltaMvVerY = 0;
  if (!SameMV)
  {
    deltaMvHorX = (affRT - affLT).hor << (shift - Log2(width));
    deltaMvHorY = (affRT - affLT).ver << (shift - Log2(width));
    int height = pu.Y().height;
    if (pu.cu->affineType == AFFINEMODEL_6PARAM)
    {
      deltaMvVerX = (affLB - affLT).hor << (shift - Log2(height));
      deltaMvVerY = (affLB - affLT).ver << (shift - Log2(height));
    }
    else
    {
      deltaMvVerX = -deltaMvHorY;
      deltaMvVerY = deltaMvHorX;
    }
  }

  int mvScaleHor = affLT.hor << shift;
  int mvScaleVer = affLT.ver << shift;
  int blockWidth = AFFINE_MIN_BLOCK_SIZE;
  int blockHeight = AFFINE_MIN_BLOCK_SIZE;
  const int halfBW = blockWidth >> 1;
  const int halfBH = blockHeight >> 1;
  MotionBuf mb = pu.getMotionBuf();
  int mvScaleTmpHor, mvScaleTmpVer;
  const bool subblkMVSpreadOverLimit = InterPredInterpolation::isSubblockVectorSpreadOverLimit(deltaMvHorX, deltaMvHorY, deltaMvVerX, deltaMvVerY, pu.interDir);

  int h = pu.Y().height / blockHeight;
  int w = pu.Y().width / blockWidth;
  for (int y = 0; y < h; y++)
  {
    for (int x = 0; x < w; x++)
    {
      if (SameMV)
      {
        mvScaleTmpHor = mvScaleHor;
        mvScaleTmpVer = mvScaleVer;
      }
      else
      {
        if (!subblkMVSpreadOverLimit)
        {
          mvScaleTmpHor = mvScaleHor + deltaMvHorX * (halfBW + x*blockWidth) + deltaMvVerX * (halfBH + y*blockHeight);
          mvScaleTmpVer = mvScaleVer + deltaMvHorY * (halfBW + x*blockWidth) + deltaMvVerY * (halfBH + y*blockHeight);

        }
        else
        {
          mvScaleTmpHor = mvScaleHor + deltaMvHorX * (pu.Y().width >> 1) + deltaMvVerX * (pu.Y().height >> 1);
          mvScaleTmpVer = mvScaleVer + deltaMvHorY * (pu.Y().width >> 1) + deltaMvVerY * (pu.Y().height >> 1);
        }
      }
      roundAffineMv(mvScaleTmpHor, mvScaleTmpVer, shift);
      Mv curMv(mvScaleTmpHor, mvScaleTmpVer);
      curMv.clipToStorageBitDepth();

      mb.at(x, y).mv[eRefList] = curMv;
    }
  }

  pu.mvAffi[eRefList][0] = affLT;
  pu.mvAffi[eRefList][1] = affRT;
  pu.mvAffi[eRefList][2] = affLB;
}

void clipColPos(int& posX, int& posY, const PredictionUnit& pu)
{
  Position puPos = pu.lumaPos();
  int log2CtuSize = pu.cs->pcv->maxCUSizeLog2;
  int ctuX = ((puPos.x >> log2CtuSize) << log2CtuSize);
  int ctuY = ((puPos.y >> log2CtuSize) << log2CtuSize);
  int horMax;
  SubPic curSubPic = pu.cu->slice->pps->getSubPicFromPos(puPos);
  if (curSubPic.treatedAsPic)
  {
    horMax = std::min((int)curSubPic.subPicRight, ctuX + (int)pu.cs->sps->CTUSize + 3);
  }
  else
  {
    horMax = std::min((int)pu.cs->pps->picWidthInLumaSamples - 1, ctuX + (int)pu.cs->sps->CTUSize + 3);
  }
  int horMin = std::max((int)0, ctuX);
  int verMax = std::min((int)pu.cs->pps->picHeightInLumaSamples - 1, ctuY + (int)pu.cs->sps->CTUSize - 1);
  int verMin = std::max((int)0, ctuY);

  posX = std::min(horMax, std::max(horMin, posX));
  posY = std::min(verMax, std::max(verMin, posY));
}


void PU::spanMotionInfo( PredictionUnit &pu, const MergeCtx &mrgCtx )
{
  MotionBuf mb = pu.getMotionBuf();
  if (!pu.mergeFlag || pu.mergeType == MRG_TYPE_DEFAULT_N)
  {
    MotionInfo mi;

    mi.isInter  = CU::isInter(*pu.cu);
    mi.sliceIdx = pu.cu->slice->independentSliceIdx;

    if( mi.isInter )
    {
      mi.interDir = pu.interDir;

      for( int i = 0; i < NUM_REF_PIC_LIST_01; i++ )
      {
        mi.mv[i]     = pu.mv[i];
        mi.refIdx[i] = pu.refIdx[i];
      }
      if (pu.cu->affine)
      {
        for (int y = 0; y < mb.height; y++)
        {
          for (int x = 0; x < mb.width; x++)
          {
            MotionInfo &dest = mb.at(x, y);
            dest.isInter = mi.isInter;
            dest.interDir = mi.interDir;
            dest.sliceIdx = mi.sliceIdx;
            for (int i = 0; i < NUM_REF_PIC_LIST_01; i++)
            {
              if (mi.refIdx[i] == -1)
              {
                dest.mv[i] = Mv();
              }
              dest.refIdx[i] = mi.refIdx[i];
            }
          }
        }
        return;
      }
    }

    mb.fill( mi );
  }
  else if (pu.mergeType == MRG_TYPE_SUBPU_ATMVP)
  {
    CHECK(mrgCtx.subPuMvpMiBuf.area() == 0 || !mrgCtx.subPuMvpMiBuf.buf, "Buffer not initialized");
    mb.copyFrom(mrgCtx.subPuMvpMiBuf);
  }
}

bool PU::isBiPredFromDifferentDirEqDistPoc(const PredictionUnit& pu)
{
  if (pu.refIdx[0] >= 0 && pu.refIdx[1] >= 0)
  {
    if (pu.cu->slice->getRefPic(REF_PIC_LIST_0, pu.refIdx[0])->isLongTerm
      || pu.cu->slice->getRefPic(REF_PIC_LIST_1, pu.refIdx[1])->isLongTerm)
    {
      return false;
    }
    const int poc0 = pu.cu->slice->getRefPOC(REF_PIC_LIST_0, pu.refIdx[0]);
    const int poc1 = pu.cu->slice->getRefPOC(REF_PIC_LIST_1, pu.refIdx[1]);
    const int poc = pu.cu->slice->poc;
    if ((poc - poc0)*(poc - poc1) < 0)
    {
      if (abs(poc - poc0) == abs(poc - poc1))
      {
        return true;
      }
    }
  }
  return false;
}

void PU::restrictBiPredMergeCandsOne(PredictionUnit &pu)
{
  if (PU::isBipredRestriction(pu))
  {
    if (pu.interDir == 3)
    {
      pu.interDir = 1;
      pu.refIdx[1] = -1;
      pu.mv[1] = Mv(0, 0);
      pu.cu->BcwIdx = BCW_DEFAULT;
    }
  }
}

void PU::getGeoMergeCandidates(const PredictionUnit &pu, MergeCtx &geoMrgCtx)
{
  MergeCtx tmpMergeCtx;

  const Slice &  slice           = *pu.cs->slice;
  const uint32_t maxNumMergeCand = slice.sps->maxNumMergeCand;

  geoMrgCtx.numValidMergeCand = 0;

  for (int32_t i = 0; i < GEO_MAX_NUM_UNI_CANDS; i++)
  {
    geoMrgCtx.BcwIdx[i]                              = BCW_DEFAULT;
    geoMrgCtx.interDirNeighbours[i]                  = 0;
    geoMrgCtx.mrgTypeNeighbours[i]                   = MRG_TYPE_DEFAULT_N;
    geoMrgCtx.mvFieldNeighbours[(i << 1)].refIdx     = NOT_VALID;
    geoMrgCtx.mvFieldNeighbours[(i << 1) + 1].refIdx = NOT_VALID;
    geoMrgCtx.mvFieldNeighbours[(i << 1)].mv         = Mv();
    geoMrgCtx.mvFieldNeighbours[(i << 1) + 1].mv     = Mv();
    geoMrgCtx.useAltHpelIf[i]                        = false;
  }

  PU::getInterMergeCandidates(pu, tmpMergeCtx, 0);

  for (int32_t i = 0; i < maxNumMergeCand; i++)
  {
    int parity = i & 1;
    if (tmpMergeCtx.interDirNeighbours[i] & (0x01 + parity))
    {
      geoMrgCtx.interDirNeighbours[geoMrgCtx.numValidMergeCand]                    = 1 + parity;
      geoMrgCtx.mrgTypeNeighbours[geoMrgCtx.numValidMergeCand]                     = MRG_TYPE_DEFAULT_N;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + !parity].mv = Mv(0, 0);
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + parity].mv =
        tmpMergeCtx.mvFieldNeighbours[(i << 1) + parity].mv;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + !parity].refIdx = -1;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + parity].refIdx =
        tmpMergeCtx.mvFieldNeighbours[(i << 1) + parity].refIdx;
      geoMrgCtx.numValidMergeCand++;
      if (geoMrgCtx.numValidMergeCand == GEO_MAX_NUM_UNI_CANDS)
      {
        return;
      }
      continue;
    }

    if (tmpMergeCtx.interDirNeighbours[i] & (0x02 - parity))
    {
      geoMrgCtx.interDirNeighbours[geoMrgCtx.numValidMergeCand] = 2 - parity;
      geoMrgCtx.mrgTypeNeighbours[geoMrgCtx.numValidMergeCand]  = MRG_TYPE_DEFAULT_N;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + !parity].mv =
        tmpMergeCtx.mvFieldNeighbours[(i << 1) + !parity].mv;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + parity].mv = Mv(0, 0);
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + !parity].refIdx =
        tmpMergeCtx.mvFieldNeighbours[(i << 1) + !parity].refIdx;
      geoMrgCtx.mvFieldNeighbours[(geoMrgCtx.numValidMergeCand << 1) + parity].refIdx = -1;
      geoMrgCtx.numValidMergeCand++;
      if (geoMrgCtx.numValidMergeCand == GEO_MAX_NUM_UNI_CANDS)
      {
        return;
      }
    }
  }
}

void PU::spanGeoMotionInfo(PredictionUnit &pu, MergeCtx &geoMrgCtx, const uint8_t splitDir, const uint8_t candIdx0,
                           const uint8_t candIdx1)
{
  pu.geoSplitDir  = splitDir;
  pu.geoMergeIdx0 = candIdx0;
  pu.geoMergeIdx1 = candIdx1;
  MotionBuf mb    = pu.getMotionBuf();

  MotionInfo biMv;
  biMv.isInter  = true;
  biMv.sliceIdx = pu.cs->slice->independentSliceIdx;

  if (geoMrgCtx.interDirNeighbours[candIdx0] == 1 && geoMrgCtx.interDirNeighbours[candIdx1] == 2)
  {
    biMv.interDir  = 3;
    biMv.mv[0]     = geoMrgCtx.mvFieldNeighbours[candIdx0 << 1].mv;
    biMv.mv[1]     = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].mv;
    biMv.refIdx[0] = geoMrgCtx.mvFieldNeighbours[candIdx0 << 1].refIdx;
    biMv.refIdx[1] = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].refIdx;
  }
  else if (geoMrgCtx.interDirNeighbours[candIdx0] == 2 && geoMrgCtx.interDirNeighbours[candIdx1] == 1)
  {
    biMv.interDir  = 3;
    biMv.mv[0]     = geoMrgCtx.mvFieldNeighbours[candIdx1 << 1].mv;
    biMv.mv[1]     = geoMrgCtx.mvFieldNeighbours[(candIdx0 << 1) + 1].mv;
    biMv.refIdx[0] = geoMrgCtx.mvFieldNeighbours[candIdx1 << 1].refIdx;
    biMv.refIdx[1] = geoMrgCtx.mvFieldNeighbours[(candIdx0 << 1) + 1].refIdx;
  }
  else if (geoMrgCtx.interDirNeighbours[candIdx0] == 1 && geoMrgCtx.interDirNeighbours[candIdx1] == 1)
  {
    biMv.interDir  = 1;
    biMv.mv[0]     = geoMrgCtx.mvFieldNeighbours[candIdx1 << 1].mv;
    biMv.mv[1]     = Mv(0, 0);
    biMv.refIdx[0] = geoMrgCtx.mvFieldNeighbours[candIdx1 << 1].refIdx;
    biMv.refIdx[1] = -1;
  }
  else if (geoMrgCtx.interDirNeighbours[candIdx0] == 2 && geoMrgCtx.interDirNeighbours[candIdx1] == 2)
  {
    biMv.interDir  = 2;
    biMv.mv[0]     = Mv(0, 0);
    biMv.mv[1]     = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].mv;
    biMv.refIdx[0] = -1;
    biMv.refIdx[1] = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].refIdx;
  }

  int16_t angle   = g_GeoParams[splitDir][0];
  int     tpmMask = 0;
  int     lookUpY = 0, motionIdx = 0;
  bool    isFlip      = angle >= 13 && angle <= 27;
  int     distanceIdx = g_GeoParams[splitDir][1];
  int     distanceX   = angle;
  int     distanceY   = (distanceX + (GEO_NUM_ANGLES >> 2)) % GEO_NUM_ANGLES;
  int     offsetX     = (-(int) pu.lwidth()) >> 1;
  int     offsetY     = (-(int) pu.lheight()) >> 1;
  if (distanceIdx > 0)
  {
    if (angle % 16 == 8 || (angle % 16 != 0 && pu.lheight() >= pu.lwidth()))
      offsetY += angle < 16 ? ((distanceIdx * pu.lheight()) >> 3) : -(int) ((distanceIdx * pu.lheight()) >> 3);
    else
      offsetX += angle < 16 ? ((distanceIdx * pu.lwidth()) >> 3) : -(int) ((distanceIdx * pu.lwidth()) >> 3);
  }
  for (int y = 0; y < mb.height; y++)
  {
    lookUpY = (((4 * y + offsetY) << 1) + 5) * g_Dis[distanceY];
    for (int x = 0; x < mb.width; x++)
    {
      motionIdx = (((4 * x + offsetX) << 1) + 5) * g_Dis[distanceX] + lookUpY;
      tpmMask   = abs(motionIdx) < 32 ? 2 : (motionIdx <= 0 ? (1 - isFlip) : isFlip);
      if (tpmMask == 2)
      {
        mb.at(x, y).isInter   = true;
        mb.at(x, y).interDir  = biMv.interDir;
        mb.at(x, y).refIdx[0] = biMv.refIdx[0];
        mb.at(x, y).refIdx[1] = biMv.refIdx[1];
        mb.at(x, y).mv[0]     = biMv.mv[0];
        mb.at(x, y).mv[1]     = biMv.mv[1];
        mb.at(x, y).sliceIdx  = biMv.sliceIdx;
      }
      else if (tpmMask == 0)
      {
        mb.at(x, y).isInter   = true;
        mb.at(x, y).interDir  = geoMrgCtx.interDirNeighbours[candIdx0];
        mb.at(x, y).refIdx[0] = geoMrgCtx.mvFieldNeighbours[candIdx0 << 1].refIdx;
        mb.at(x, y).refIdx[1] = geoMrgCtx.mvFieldNeighbours[(candIdx0 << 1) + 1].refIdx;
        mb.at(x, y).mv[0]     = geoMrgCtx.mvFieldNeighbours[candIdx0 << 1].mv;
        mb.at(x, y).mv[1]     = geoMrgCtx.mvFieldNeighbours[(candIdx0 << 1) + 1].mv;
        mb.at(x, y).sliceIdx  = biMv.sliceIdx;
      }
      else
      {
        mb.at(x, y).isInter   = true;
        mb.at(x, y).interDir  = geoMrgCtx.interDirNeighbours[candIdx1];
        mb.at(x, y).refIdx[0] = geoMrgCtx.mvFieldNeighbours[candIdx1 << 1].refIdx;
        mb.at(x, y).refIdx[1] = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].refIdx;
        mb.at(x, y).mv[0]     = geoMrgCtx.mvFieldNeighbours[candIdx1 << 1].mv;
        mb.at(x, y).mv[1]     = geoMrgCtx.mvFieldNeighbours[(candIdx1 << 1) + 1].mv;
        mb.at(x, y).sliceIdx  = biMv.sliceIdx;
      }
    }
  }
}

void CU::resetMVDandMV2Int( CodingUnit& cu )
{
  PredictionUnit &pu = *cu.pu;
  {
    MergeCtx mrgCtx;

    if( !pu.mergeFlag )
    {
      if( pu.interDir != 2 /* PRED_L1 */ )
      {
        Mv mv        = pu.mv[0];
        Mv mvPred;
        AMVPInfo amvpInfo;
        PU::fillMvpCand(pu, REF_PIC_LIST_0, pu.refIdx[0], amvpInfo);
        pu.mvpNum[0] = amvpInfo.numCand;

        mvPred       = amvpInfo.mvCand[pu.mvpIdx[0]];
        mv.roundTransPrecInternal2Amvr(cu.imv);
        pu.mv[0]     = mv;
        Mv mvDiff    = mv - mvPred;
        pu.mvd[0]    = mvDiff;
      }
      if( pu.interDir != 1 /* PRED_L0 */ )
      {
        Mv mv        = pu.mv[1];
        Mv mvPred;
        AMVPInfo amvpInfo;
        PU::fillMvpCand(pu, REF_PIC_LIST_1, pu.refIdx[1], amvpInfo);
        pu.mvpNum[1] = amvpInfo.numCand;

        mvPred       = amvpInfo.mvCand[pu.mvpIdx[1]];
        mv.roundTransPrecInternal2Amvr(cu.imv);
        Mv mvDiff    = mv - mvPred;

        if( pu.cu->cs->slice->picHeader->mvdL1Zero && pu.interDir == 3 /* PRED_BI */ )
        {
          pu.mvd[1] = Mv();
          mv = mvPred;
        }
        else
        {
          pu.mvd[1] = mvDiff;
        }
        pu.mv[1] = mv;
      }

    }
    else
    {
      PU::getInterMergeCandidates ( pu, mrgCtx, 0 );
      mrgCtx.setMergeInfo( pu, pu.mergeIdx );
    }

    PU::spanMotionInfo( pu, mrgCtx );
  }
}

bool CU::hasSubCUNonZeroMVd( const CodingUnit& cu )
{
  bool bNonZeroMvd = false;

  const auto &pu = *cu.pu;
  {
    if( ( !pu.mergeFlag ) && ( !cu.skip ) )
    {
      if( pu.interDir != 2 /* PRED_L1 */ )
      {
        bNonZeroMvd |= pu.mvd[REF_PIC_LIST_0].hor != 0;
        bNonZeroMvd |= pu.mvd[REF_PIC_LIST_0].ver != 0;
      }
      if( pu.interDir != 1 /* PRED_L0 */ )
      {
        if( !pu.cu->cs->slice->picHeader->mvdL1Zero || pu.interDir != 3 /* PRED_BI */ )
        {
          bNonZeroMvd |= pu.mvd[REF_PIC_LIST_1].hor != 0;
          bNonZeroMvd |= pu.mvd[REF_PIC_LIST_1].ver != 0;
        }
      }
    }
  }

  return bNonZeroMvd;
}

bool CU::hasSubCUNonZeroAffineMVd( const CodingUnit& cu )
{
  bool nonZeroAffineMvd = false;

  if ( !cu.affine || cu.pu->mergeFlag )
  {
    return false;
  }

  const auto &pu = *cu.pu;
  {
    if ( ( !pu.mergeFlag ) && ( !cu.skip ) )
    {
      if ( pu.interDir != 2 /* PRED_L1 */ )
      {
        for ( int i = 0; i < ( cu.affineType == AFFINEMODEL_6PARAM ? 3 : 2 ); i++ )
        {
          nonZeroAffineMvd |= pu.mvdAffi[REF_PIC_LIST_0][i].hor != 0;
          nonZeroAffineMvd |= pu.mvdAffi[REF_PIC_LIST_0][i].ver != 0;
        }
      }

      if ( pu.interDir != 1 /* PRED_L0 */ )
      {
        if ( !pu.cu->cs->slice->picHeader->mvdL1Zero || pu.interDir != 3 /* PRED_BI */ )
        {
          for ( int i = 0; i < ( cu.affineType == AFFINEMODEL_6PARAM ? 3 : 2 ); i++ )
          {
            nonZeroAffineMvd |= pu.mvdAffi[REF_PIC_LIST_1][i].hor != 0;
            nonZeroAffineMvd |= pu.mvdAffi[REF_PIC_LIST_1][i].ver != 0;
          }
        }
      }
    }
  }

  return nonZeroAffineMvd;
}

uint8_t CU::numSbtModeRdo( uint8_t sbtAllowed )
{
  uint8_t num = 0;
  uint8_t sum = 0;
  num = targetSbtAllowed( SBT_VER_HALF, sbtAllowed ) + targetSbtAllowed( SBT_HOR_HALF, sbtAllowed );
  sum += std::min( SBT_NUM_RDO, ( num << 1 ) );
  num = targetSbtAllowed( SBT_VER_QUAD, sbtAllowed ) + targetSbtAllowed( SBT_HOR_QUAD, sbtAllowed );
  sum += std::min( SBT_NUM_RDO, ( num << 1 ) );
  return sum;
}

bool CU::isSbtMode( const uint8_t sbtInfo )
{
  uint8_t sbtIdx = getSbtIdx( sbtInfo );
  return sbtIdx >= SBT_VER_HALF && sbtIdx <= SBT_HOR_QUAD;
}

bool CU::isSameSbtSize( const uint8_t sbtInfo1, const uint8_t sbtInfo2 )
{
  uint8_t sbtIdx1 = getSbtIdxFromSbtMode( sbtInfo1 );
  uint8_t sbtIdx2 = getSbtIdxFromSbtMode( sbtInfo2 );
  if( sbtIdx1 == SBT_HOR_HALF || sbtIdx1 == SBT_VER_HALF )
    return sbtIdx2 == SBT_HOR_HALF || sbtIdx2 == SBT_VER_HALF;
  else if( sbtIdx1 == SBT_HOR_QUAD || sbtIdx1 == SBT_VER_QUAD )
    return sbtIdx2 == SBT_HOR_QUAD || sbtIdx2 == SBT_VER_QUAD;
  else
    return false;
}

PartSplit CU::getSbtTuSplit( const uint8_t sbtInfo ) 
{
  uint8_t sbtTuSplitType = CU::getSbtPos( sbtInfo );
  switch( CU::getSbtIdx( sbtInfo ) )
  {
  case SBT_VER_HALF: sbtTuSplitType += SBT_VER_HALF_POS0_SPLIT; break;
  case SBT_HOR_HALF: sbtTuSplitType += SBT_HOR_HALF_POS0_SPLIT; break;
  case SBT_VER_QUAD: sbtTuSplitType += SBT_VER_QUAD_POS0_SPLIT; break;
  case SBT_HOR_QUAD: sbtTuSplitType += SBT_HOR_QUAD_POS0_SPLIT; break;
  default: assert( 0 );  break;
  }

  assert( sbtTuSplitType <= SBT_HOR_QUAD_POS1_SPLIT && sbtTuSplitType >= SBT_VER_HALF_POS0_SPLIT );
  return PartSplit(sbtTuSplitType);
}

bool CU::isPredRegDiffFromTB(const CodingUnit &cu, const ComponentID compID)
{
  return (compID == COMP_Y)
    && (cu.ispMode == VER_INTRA_SUBPARTITIONS &&
      CU::isMinWidthPredEnabledForBlkSize(cu.blocks[compID].width, cu.blocks[compID].height)
      );
}
bool CU::isMinWidthPredEnabledForBlkSize(const int w, const int h)
{
  return ((w == 8 && h > 4) || w == 4);
}
bool CU::isFirstTBInPredReg(const CodingUnit& cu, const ComponentID compID, const CompArea& area)
{
  return (compID == COMP_Y) && cu.ispMode && ((area.topLeft().x - cu.Y().topLeft().x) % PRED_REG_MIN_WIDTH == 0);
}
void CU::adjustPredArea(CompArea& area)
{
  area.width = std::max<int>(PRED_REG_MIN_WIDTH, area.width);
}

bool CU::isBcwIdxCoded( const CodingUnit &cu )
{
  if( ! cu.cs->sps->BCW )
  {
    CHECK(cu.BcwIdx != BCW_DEFAULT, "Error: cu.BcwIdx != BCW_DEFAULT");
    return false;
  }

  if (cu.predMode == MODE_IBC)
  {
    return false;
  }

  if( cu.predMode == MODE_INTRA || cu.cs->slice->isInterP() )
  {
    return false;
  }

  if( cu.lwidth() * cu.lheight() < BCW_SIZE_CONSTRAINT )
  {
    return false;
  }

  if( !cu.pu->mergeFlag )
  {
    if( cu.pu->interDir == 3 )
    {
      WPScalingParam *wp0;
      WPScalingParam *wp1;
      int refIdx0 = cu.pu->refIdx[REF_PIC_LIST_0];
      int refIdx1 = cu.pu->refIdx[REF_PIC_LIST_1];

      cu.cs->slice->getWpScaling(REF_PIC_LIST_0, refIdx0, wp0);
      cu.cs->slice->getWpScaling(REF_PIC_LIST_1, refIdx1, wp1);
      if ((wp0[COMP_Y].presentFlag || wp0[COMP_Cb].presentFlag || wp0[COMP_Cr].presentFlag
        || wp1[COMP_Y].presentFlag || wp1[COMP_Cb].presentFlag || wp1[COMP_Cr].presentFlag))
      {
        return false;
      }
      return true;
    }
  }

  return false;
}

uint8_t CU::getValidBcwIdx( const CodingUnit &cu )
{
  if( cu.pu->interDir == 3 && !cu.pu->mergeFlag )
  {
    return cu.BcwIdx;
  }
  else if( cu.pu->interDir == 3 && cu.pu->mergeFlag && cu.pu->mergeType == MRG_TYPE_DEFAULT_N )
  {
    // This is intended to do nothing here.
  }
  else
  {
    CHECK(cu.BcwIdx != BCW_DEFAULT, " cu.BcwIdx != BCW_DEFAULT ");
  }

  return BCW_DEFAULT;
}

void CU::setBcwIdx( CodingUnit &cu, uint8_t uh )
{
  int8_t uhCnt = 0;

  if( cu.pu->interDir == 3 && !cu.pu->mergeFlag )
  {
    cu.BcwIdx = uh;
    ++uhCnt;
  }
  else if( cu.pu->interDir == 3 && cu.pu->mergeFlag && cu.pu->mergeType == MRG_TYPE_DEFAULT_N )
  {
    // This is intended to do nothing here.
  }
  else
  {
    cu.BcwIdx = BCW_DEFAULT;
  }

  CHECK(uhCnt <= 0, " uhCnt <= 0 ");
}

bool CU::bdpcmAllowed( const CodingUnit& cu, const ComponentID compID )
{
  SizeType transformSkipMaxSize = 1 << cu.cs->sps->log2MaxTransformSkipBlockSize;

  const Size& blkSize = isLuma(compID) ? cu.lumaSize() : cu.chromaSize();
  bool bdpcmAllowed = cu.cs->sps->BDPCM;
       bdpcmAllowed &= CU::isIntra( cu );
       bdpcmAllowed &= (blkSize.width <= transformSkipMaxSize && blkSize.height <= transformSkipMaxSize);
  return bdpcmAllowed;
}

bool CU::isMTSAllowed(const CodingUnit &cu, const ComponentID compID)
{
  SizeType tsMaxSize = 1 << cu.cs->sps->log2MaxTransformSkipBlockSize;
  const int maxSize  = CU::isIntra( cu ) ? MTS_INTRA_MAX_CU_SIZE : MTS_INTER_MAX_CU_SIZE;
  const int cuWidth  = cu.blocks[0].lumaSize().width;
  const int cuHeight = cu.blocks[0].lumaSize().height;
  bool mtsAllowed    = cu.chType == CH_L && compID == COMP_Y;

  mtsAllowed &= CU::isIntra( cu ) ? cu.cs->sps->MTSIntra : cu.cs->sps->MTSInter && CU::isInter( cu );
  mtsAllowed &= cuWidth <= maxSize && cuHeight <= maxSize;
  mtsAllowed &= !cu.ispMode;
  mtsAllowed &= !cu.sbtInfo;
  mtsAllowed &= !(cu.bdpcmMode && cuWidth <= tsMaxSize && cuHeight <= tsMaxSize);
  return mtsAllowed;
}


// TU tools

bool TU::isNonTransformedResidualRotated(const TransformUnit& tu, const ComponentID compID)
{
  return tu.cs->sps->spsRExt.transformSkipRotationEnabled && tu.blocks[compID].width == 4 && tu.cu->predMode == MODE_INTRA;
}

bool TU::getCbf( const TransformUnit& tu, const ComponentID compID )
{
  return getCbfAtDepth( tu, compID, tu.depth );
}

bool TU::getCbfAtDepth(const TransformUnit& tu, const ComponentID compID, const unsigned depth)
{
  if( !tu.blocks[compID].valid() )
    CHECK( tu.cbf[compID] != 0, "cbf must be 0 if the component is not available" );
  return ((tu.cbf[compID] >> depth) & 1) == 1;
}

void TU::setCbfAtDepth(TransformUnit& tu, const ComponentID compID, const unsigned depth, const bool cbf)
{
  // first clear the CBF at the depth
  tu.cbf[compID] &= ~(1  << depth);
  // then set the CBF
  tu.cbf[compID] |= ((cbf ? 1 : 0) << depth);
}

bool TU::isTSAllowed(const TransformUnit &tu, const ComponentID compID)
{
  const int maxSize = tu.cs->sps->log2MaxTransformSkipBlockSize;

  bool tsAllowed = tu.cs->sps->transformSkip;
  tsAllowed &= ( !tu.cu->ispMode || !isLuma(compID) );
  SizeType transformSkipMaxSize = 1 << maxSize;
  tsAllowed &= !(tu.cu->bdpcmMode && isLuma(compID));
  tsAllowed &= !(tu.cu->bdpcmModeChroma && isChroma(compID));
  tsAllowed &= tu.blocks[compID].width <= transformSkipMaxSize && tu.blocks[compID].height <= transformSkipMaxSize;
  tsAllowed &= !tu.cu->sbtInfo;

  return tsAllowed;
}


int TU::getICTMode( const TransformUnit& tu, int jointCbCr )
{
  if( jointCbCr < 0 )
  {
    jointCbCr = tu.jointCbCr;
  }
  return g_ictModes[ tu.cs->picHeader->jointCbCrSign ][ jointCbCr ];
}

bool TU::needsSqrt2Scale( const TransformUnit& tu, const ComponentID compID )
{
  const Size& size=tu.blocks[compID];
  const bool isTransformSkip = tu.mtsIdx[compID]==MTS_SKIP && isLuma(compID);
  return (!isTransformSkip) && (((Log2(size.width * size.height)) & 1) == 1);
}

TransformUnit* TU::getPrevTU( const TransformUnit& tu, const ComponentID compID )
{
  TransformUnit* prevTU = tu.prev;

  if( prevTU != nullptr && ( prevTU->cu != tu.cu || !prevTU->blocks[compID].valid() ) )
  {
    prevTU = nullptr;
  }

  return prevTU;
}

bool TU::getPrevTuCbfAtDepth( const TransformUnit& currentTu, const ComponentID compID, const int trDepth )
{
  const TransformUnit* prevTU = getPrevTU( currentTu, compID );
  return ( prevTU != nullptr ) ? TU::getCbfAtDepth( *prevTU, compID, trDepth ) : false;
}


// other tools

uint32_t getCtuAddr( const Position& pos, const PreCalcValues& pcv )
{
  return ( pos.x >> pcv.maxCUSizeLog2 ) + ( pos.y >> pcv.maxCUSizeLog2 ) * pcv.widthInCtus;
}

int getNumModesMip(const Size& block)
{
  switch( getMipSizeId(block) )
  {
  case 0: return 16;
  case 1: return  8;
  case 2: return  6;
  default: THROW( "Invalid mipSizeId" );
  }
}

int getMipSizeId(const Size& block)
{
  if( block.width == 4 && block.height == 4 )
  {
    return 0;
  }
  else if( block.width == 4 || block.height == 4 || (block.width == 8 && block.height == 8) )
  {
    return 1;
  }
  else
  {
    return 2;
  }
}

bool allowLfnstWithMip(const Size& block)
{
  if (block.width >= 16 && block.height >= 16)
  {
    return true;
  }
  return false;
}


} // namespace vvenc

//! \}

