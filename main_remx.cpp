#include <iostream>
#include <fstream>
#include <sstream>
#include "mpegts_demuxer.h"
#include "mpegts_muxer.h"


//AAC (ADTS) audio
#define TYPE_AUDIO 0x0f
//H.264 video
#define TYPE_VIDEO_H264 0x1b
//H.265 video
#define TYPE_VIDEO_H265 0x24

#define PMT_PID 100

//#define SAVE_TS_FILE
//#define SAVE_ES_FILE

MpegTsDemuxer gDemuxer;

void dmxOutput(const EsFrame &esFrame) {}

int main(int argc, char *argv[]) {
#ifdef IMAX_SCT
    gDemuxer.esOutCallback = std::bind(&dmxOutput, std::placeholders::_1);

    std::cout << "Input TS file: " << argv[1] << std::endl;
    std::cout << "Output TS file: " << argv[2] << std::endl;
    std::cout << "Video PID starting CC: " << argv[3] << std::endl;
    std::cout << "SPS file: " << argv[4] << std::endl;

    std::ifstream ifile(argv[1], std::ios::binary | std::ios::in);
    std::ifstream ifile2(argv[4], std::ios::binary | std::ios::in);
    std::ofstream oVFile(argv[2], std::ofstream::binary | std::ofstream::out);

    uint8_t packet[188] = {0};
    SimpleBuffer in;

    std::vector<TSPacket> tsPacketVector;
    std::vector<EsFrame> esFrameVector;

    int count = 0;

    while (!ifile.eof()) {
        ifile.read((char*)&packet[0], 188);
        // write out the last ES frame
        if (ifile.gcount() != 188) {
            EsFrame *esFrame = gDemuxer.getH264Frame();

            esFrameVector.emplace_back(*esFrame);
            dmxOutput(*esFrame);
            break;
        }        
        in.append(packet, 188);
        gDemuxer.decode(in, tsPacketVector, esFrameVector);
        count ++;
    }
    ifile.close();
    std::cout << "Total number of video frames: " << gDemuxer.videoFrameNumber << std::endl;

    SimpleBuffer sps;
    while (!ifile2.eof()) {
        ifile2.read((char*)&packet[0], 188);
        if (ifile2.gcount() != 188) {
            sps.append(packet, ifile2.gcount());
            break; 
        }
        sps.append(packet, 188);
    }
    ifile2.close();

#ifdef SAVE_TS_FILE
    // original TS packets, outputFile.ts should be identical to input TS 
    std::ofstream oVFile1("outputFile.ts", std::ofstream::binary | std::ofstream::out);
    for (const auto& packet : tsPacketVector) {
        //std::cout << "writing a TS packet to " << "outputFile.ts" << std::endl;
        oVFile1.write((const char *)packet.tsPacket, 188);
    }
    oVFile1.close();
#endif

    int32_t videoFrameNumber = -1;
    uint32_t tsPacketIdx = 0;
    videoFrameNumber = 0;
    
    // store all the PCR values and their packet index as they are needed for TS remuxing
    for (auto& esFrame : esFrameVector) {
        uint32_t startTsIdx = 0;
        esFrame.origNumTsPackets = 0;

        for (uint32_t tsIdx = tsPacketIdx; tsIdx < tsPacketVector.size(); tsIdx ++)
        {
            if (tsPacketVector[tsIdx].containsVideoPES && tsPacketVector[tsIdx].pesFrameNumber == videoFrameNumber)
            {
                if (tsPacketVector[tsIdx].containsPCR)
                {
                    esFrame.pcrIndexes.emplace_back(startTsIdx);
                    esFrame.pcrs.emplace_back(tsPacketVector[tsIdx].pcrValue);
                    tsPacketIdx = tsIdx;
                }
                esFrame.origNumTsPackets ++;
                startTsIdx ++;
            }
        }
        videoFrameNumber ++;
    }

    MpegTsMuxer *gpMuxer;
    std::map<uint8_t, int> gStreamPidMap;
    gStreamPidMap[TYPE_AUDIO] = gDemuxer.mStreamPidMap[TYPE_AUDIO];
    gStreamPidMap[TYPE_VIDEO_H264] = gDemuxer.mStreamPidMap[TYPE_VIDEO_H264];
    std::cout << "Video PID " << gDemuxer.mStreamPidMap[TYPE_VIDEO_H264] << std::endl;
    gpMuxer = new MpegTsMuxer(gStreamPidMap, PMT_PID, gStreamPidMap[TYPE_VIDEO_H264], MpegTsMuxer::MuxType::h222Type, std::stoi(argv[3]));
    gpMuxer->tsOutCallback = nullptr;

    videoFrameNumber = 0;
    for (auto& esFrame : esFrameVector) {
        esFrame.videoFrameNumber = videoFrameNumber;

        // search and replace SPS
        // assuming that the SPS is only in the first access unit
        if (videoFrameNumber == 0) {
            SimpleBuffer lSb;
            gpMuxer->replaceSps(esFrame, lSb, sps);
            *esFrame.mData = lSb;
        }
        gpMuxer->encode(esFrame);
        videoFrameNumber ++;        
    }

#ifdef SAVE_ES_FILE
    std::ofstream oVFile2("outputFile.h264", std::ofstream::binary | std::ofstream::out);
    for (auto& esFrame : esFrameVector) 
        oVFile2.write((const char *)esFrame.mData->data(), esFrame.mData->size());
    oVFile2.close();
#endif

    int32_t prevVideoFrameNumber = 0;
    uint32_t tsIdx = 0;

    // write out the remuxed TS
    // for the video TS packets, write the remuxed TS packets
    // for all other TS packets, write the the original TS packets
    for (const auto& packet : tsPacketVector) {
        if (packet.containsVideoPES == false) {
            oVFile.write((const char *)packet.tsPacket, 188);
        }
        else {
            videoFrameNumber = packet.pesFrameNumber;

            if (videoFrameNumber != prevVideoFrameNumber) {
                tsIdx = 0;
            }
            if (tsIdx > esFrameVector[videoFrameNumber].numTsPackets - 1) {
                // don't write anything, already written all packets
                std::cout << "Frame " << videoFrameNumber << " skip packet, already written all packets" << std::endl;
            }
            else if (tsIdx == esFrameVector[videoFrameNumber].origNumTsPackets - 1 && 
                     esFrameVector[videoFrameNumber].numTsPackets > esFrameVector[videoFrameNumber].origNumTsPackets) {
                // keep writing, as there are more
                while (tsIdx < esFrameVector[videoFrameNumber].numTsPackets) {
                    oVFile.write((const char*)esFrameVector[videoFrameNumber].mTSData->data() + tsIdx * 188, 188);
                    tsIdx ++;
                }
            }
            else {
                oVFile.write((const char*)esFrameVector[videoFrameNumber].mTSData->data() + tsIdx * 188, 188);
                tsIdx ++;
            }
        }
        prevVideoFrameNumber = videoFrameNumber;
    }
    oVFile.close();
#endif

    return EXIT_SUCCESS;
}

