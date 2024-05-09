# mpeg_ts_remx_tests Command Documentation

## Overview
The `mpeg_ts_remx_tests` command is used for manipulating MPEG Transport Stream (TS) files. It allows users to modify the SPS in the TS file.

## Usage
./mpeg_ts_remx_tests <input_file> <output_file> <initial_continuity_counter> <sps_file>

## Parameters
- `input_file`: Specifies the input MPEG TS file to be processed.
- `output_file`: Specifies the output file where the modified TS will be saved.
- `initial_continuity_counter`: Sets the starting value for the continuity counter of the video PID.
- `sps_file`: Specifies the file containing the SPS (Sequence Parameter Set) byte array used for replacement.

## Example
./mpeg_ts_remx_tests channel_candidate2_08233.ts outputFile.ts 4 sps.bin
