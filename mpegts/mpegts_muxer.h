#pragma once

// Prefixes used
// m class member
// p pointer (*)
// r reference (&)
// l local scope

#include "ts_packet.h"
#include "simple_buffer.h"
#include <map>
#include <cstdint>
#include <functional>
#include <mutex>
#include "common.h"

class MpegTsMuxer {
public:
    enum class MuxType: uint8_t {
        unknown,
        segmentType, //Generate a PAT + PMT when lRandomAccess is set to true in the encode() method
        h222Type, //Not implemented correct
        dvbType //Not implemented at all
    };

#ifdef IMAX_SCT
    MpegTsMuxer(std::map<uint8_t, int> lStreamPidMap, uint16_t lPmtPid, uint16_t lPcrPid, MuxType lType, uint8_t initCc=0);
#else
    MpegTsMuxer(std::map<uint8_t, int> lStreamPidMap, uint16_t lPmtPid, uint16_t lPcrPid, MuxType lType);
#endif

    virtual ~MpegTsMuxer();

    void createPat(SimpleBuffer &rSb, uint16_t lPmtPid, uint8_t lCc);

    void createPmt(SimpleBuffer &rSb, std::map<uint8_t, int> lStreamPidMap, uint16_t lPmtPid, uint8_t lCc);

    void createPes(EsFrame &rFrame, SimpleBuffer &rSb);

    void createPcr(uint64_t lPcr, uint8_t lTag = 0);

    void createNull(uint8_t lTag = 0);

    void encode(EsFrame &rFrame, uint8_t lTag = 0, bool lRandomAccess = false);

    std::function<void(SimpleBuffer &rSb, uint8_t lTag, bool lRandomAccess)> tsOutCallback = nullptr;

#ifdef IMAX_SCT
    void replaceSps(EsFrame& esFrameDst,
                    SimpleBuffer &rSb,
                    SimpleBuffer &rSps,
                    unsigned char* sps,
                    size_t spsSize,
                    unsigned char* pps,
                    size_t ppsSize);
    void extractSps(EsFrame& esFrameSrc,
                    SimpleBuffer &rSps,
                    unsigned char* sps,
                    size_t spsSize,
                    unsigned char* pps,
                    size_t ppsSize);
#endif

private:
    uint8_t getCc(uint32_t lWithPid);
#ifdef IMAX_SCT
    void initCc(uint32_t lWithPid, uint8_t uCC);
#endif

    uint32_t mCurrentIndex = 0;
    bool shouldCreatePat();

    std::map<uint32_t, uint8_t> mPidCcMap;

    uint16_t mPmtPid = 0;

    std::map<uint8_t, int> mStreamPidMap;

    uint16_t mPcrPid;

    MuxType mMuxType = MuxType::unknown;

    std::mutex mMuxMtx;

};

