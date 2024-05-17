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

    if (argc != 6) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_file> <mode> <initial_continuity_counter> <sps_file>" << std::endl;
        return EXIT_FAILURE;
    }
    std::string input_file = argv[1];
    std::string output_file = argv[2]; 
    std::string mode = argv[3]; 
    std::string initial_cc = argv[4]; 
    std::string sps_file = argv[5];    
    
    std::cout << "Input TS file: " << input_file << std::endl;
    std::cout << "Output TS file: " << output_file << std::endl;
    std::cout << "Mode: " << mode << std::endl;
    std::cout << "Video PID starting CC: " << initial_cc << std::endl;
    std::cout << "SPS file: " << sps_file << std::endl;

    // mode: 0, 1, 2
    // mode 0: loopback, no change made to the packet
    // mode 1: remux only, no sps replacement
    // mode 2: full functionality with sps replacement

    std::ifstream ifile(input_file, std::ios::binary | std::ios::in);
    std::ifstream ifile2(sps_file, std::ios::binary | std::ios::in);

    uint8_t packet[188] = {0};
    SimpleBuffer in;

    std::vector<TSPacket> tsPacketVector;
    std::vector<EsFrame> esFrameVector;

    int count = 0;

#ifdef HEVC
    int streamType = TYPE_VIDEO_H265;  
#else
    int streamType = TYPE_VIDEO_H264;  
#endif

    while (!ifile.eof()) {
        ifile.read((char*)&packet[0], 188);
        // write out the last ES frame
        if (ifile.gcount() != 188) {
            EsFrame *esFrame = gDemuxer.getEsFrame(streamType);
            if (esFrame->mData->size() != 0) {
                esFrameVector.emplace_back(*esFrame);
                dmxOutput(*esFrame);
            }
            break;
        }        
        in.append(packet, 188);
        gDemuxer.decode(in, tsPacketVector, esFrameVector);
        count ++;
    }
    ifile.close();
    std::cout << "Input TS packet count: " << count << std::endl;
    std::cout << "Size of esFrameVector: " << esFrameVector.size() << std::endl;

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

    // lookback mode
    if (std::stoi(mode) == 0) {
        // original TS packets, outputFile.ts should be identical to input TS 
        std::ofstream oVFile1(output_file, std::ofstream::binary | std::ofstream::out);
        for (const auto& packet : tsPacketVector) {
            //std::cout << "writing a TS packet to " << "outputFile.ts" << std::endl;
            oVFile1.write((const char *)packet.tsPacket, 188);
        }
        oVFile1.close();
        std::cout << "Output TS packet count: " << tsPacketVector.size() << std::endl;

        return EXIT_SUCCESS;
    }

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
#ifdef HEVC
    gStreamPidMap[TYPE_VIDEO_H265] = gDemuxer.mStreamPidMap[TYPE_VIDEO_H265];
    std::cout << "Video PID " << gDemuxer.mStreamPidMap[TYPE_VIDEO_H265] << std::endl;
    gpMuxer = new MpegTsMuxer(gStreamPidMap, PMT_PID, gStreamPidMap[TYPE_VIDEO_H265], MpegTsMuxer::MuxType::h222Type, std::stoi(initial_cc));
#else
    gStreamPidMap[TYPE_VIDEO_H264] = gDemuxer.mStreamPidMap[TYPE_VIDEO_H264];
    std::cout << "Video PID " << gDemuxer.mStreamPidMap[TYPE_VIDEO_H264] << std::endl;
    gpMuxer = new MpegTsMuxer(gStreamPidMap, PMT_PID, gStreamPidMap[TYPE_VIDEO_H264], MpegTsMuxer::MuxType::h222Type, std::stoi(initial_cc));
#endif    
    gpMuxer->tsOutCallback = nullptr;

    videoFrameNumber = 0;
    for (auto& esFrame : esFrameVector) {
        esFrame.videoFrameNumber = videoFrameNumber;

        // search and replace SPS
        // assuming that the SPS is only in the first access unit
        if (std::stoi(mode) > 1 && videoFrameNumber == 0) {
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
    std::ofstream oVFile(output_file, std::ofstream::binary | std::ofstream::out);

    // write out the remuxed TS
    // for the video TS packets, write the remuxed TS packets
    // for all other TS packets, write the the original TS packets
    count = 0;
    for (const auto& packet : tsPacketVector) {
        if (packet.containsVideoPES == false) {
            oVFile.write((const char *)packet.tsPacket, 188);
            count ++;
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
                    count ++;
                    tsIdx ++;
                }
            }
            else {
                oVFile.write((const char*)esFrameVector[videoFrameNumber].mTSData->data() + tsIdx * 188, 188);
                count ++;
                tsIdx ++;
            }
        }
        prevVideoFrameNumber = videoFrameNumber;
    }
    oVFile.close();
    std::cout << "Output TS packet count: " << count << std::endl;

#endif

    return EXIT_SUCCESS;
}

