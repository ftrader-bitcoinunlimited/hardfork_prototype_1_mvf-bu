// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2015-2017 The Bitcoin Unlimited developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "random.h"
#include "util.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1262152739;  // Block #32255
    pindexLast.nBits = 0x1d00ffff;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1d00d86a);
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996;  // Block #2015
    pindexLast.nBits = 0x1d00ffff;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1d00ffff);
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671;  // Block #68543
    pindexLast.nBits = 0x1c05a3f4;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1c0168fd);
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443;  // Block #46367
    pindexLast.nBits = 0x1c387f6f;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1d00e1fd);
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : NULL;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * params.nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[GetRand(10000)];
        CBlockIndex *p2 = &blocks[GetRand(10000)];
        CBlockIndex *p3 = &blocks[GetRand(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, params);
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

// MVF-BU begin
/* added unit test after we found that on regtest, difficulty calculation
   can lead to overflow of 256-bit integer. Here an excessive retarget time
   is set in order to trigger the overflow case, in which case the previous
   difficulty is re-used. */
BOOST_AUTO_TEST_CASE(MVFCheckOverflowCalculation_test)
{
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus();
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit); // MVF-BU moved here

    // test scenario post fork
    FinalActivateForkHeight = 2016;

    int64_t nLastRetargetTime = 7;  // Force an excessive retarget time to trigger overflow
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2024;
    pindexLast.nTime = 1279297671;  // Block #68543
    pindexLast.nBits = 0x207aaaaa;  // Almost overflowing already

    // an overflow causes the POW limit to be returned
    // need to set -force-retarget, otherwise cannot test overflow
    // because it would never reach the computation
    SoftSetBoolArg("-force-retarget", true);
    BOOST_CHECK_EQUAL(CalculateMVFNextWorkRequired(&pindexLast, nLastRetargetTime, params), bnPowLimit.GetCompact());
}

/* added unit test for fork reset. doesn't test easily in regtest
 * because takes some retargets before raising bits off the limit  */
BOOST_AUTO_TEST_CASE(MVFCheckCalculateMVFResetWorkRequired)
{
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus();
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit); // MVF-BU moved here

    // define last block
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671;  // Block #68543
    pindexLast.nBits = 0x1c05a3f4;

    // retarget time for test
    int64_t nLastRetargetTime = pindexLast.nTime - (params.nPowTargetSpacing * params.DifficultyAdjustmentInterval());

    // force retargeting in CalculateMVFNextWorkRequired
    SoftSetBoolArg("-force-retarget", true);

    // test for drop factor
    FinalDifficultyDropFactor = HARDFORK_DROPFACTOR_REGTEST;
    BOOST_CHECK_EQUAL(CalculateMVFResetWorkRequired(&pindexLast, nLastRetargetTime, params), 0x1c168fcf);
}
// MVF-BU end

BOOST_AUTO_TEST_SUITE_END()
