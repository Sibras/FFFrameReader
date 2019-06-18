FFFrameReader
=============
[![Github All Releases](https://img.shields.io/github/downloads/Sibras/FFFrameReader/total.svg)](https://github.com/Sibras/FFFrameReader/releases)
[![GitHub release](https://img.shields.io/github/release/Sibras/FFFrameReader.svg)](https://github.com/Sibras/FFFrameReader/releases/latest)
[![GitHub issues](https://img.shields.io/github/issues/Sibras/FFFrameReader.svg)](https://github.com/Sibras/FFFrameReader/issues)
[![license](https://img.shields.io/github/license/Sibras/FFFrameReader.svg)](https://github.com/Sibras/FFFrameReader/blob/master/LICENSE)
[![donate](https://img.shields.io/badge/donate-link-brightgreen.svg)](https://shiftmediaproject.github.io/8-donate/)

## About

This project provides a library the wraps Ffmpeg decoding functionality into a simple to use c++ library.
It is designed to allow for easily retrieving frames from a video file that can then be used as required by a host program.
The library supports software decoding as well as some hardware decoders with basic conversion and processing support.

## Example:
To use this library you first need to create a Stream object that is used to represent the decoded file. 
A default Stream object will use normal software decoding on the CPU and can be created using the following:
~~~~
auto stream = Stream::getStream("filename");
if (stream == nullptr) {
    // File opening has failed
}
~~~~
Streams can also be optionally passed a set of options (@see DecoderOptions) that can be used to enable
hardware accelerated decoding, resizing, cropping and various other decoding parameters. For instance to 
use NVIDIA GPU accelerated decoding you can create decoder options as follows:
~~~~
DecoderOptions options(DecodeType::Cuda);
~~~~
Options for software decoding but with resizing can be created as follows:
~~~~
DecoderOptions options;
options.m_scale = {1280, 720};
~~~~
Once decoder options have been created they can then be used to open various files by passing them to the stream creation:
~~~~
auto stream = Stream::getStream(fileName, options);
~~~~
A stream object can then be used to get information about the opened file (such as resolution, duration etc.) and to
read image frames from the video. To get the next frame in a video you can use the following in a loop:
~~~~
auto frame = stream->getNextFrame();
if (frame == nullptr) {
    // Failed to get the next frame, there was either an internal error or the end of the file has been reached
}
~~~~
This creates a frame object that has the information for the requested frame (such as its time stamp, frame number
etc.). It also has the actual image data itself that can be retrieved as follows:
~~~~
auto data1 = frame->getFrameData(00);
~~~~
The format of the data stored in the returned pointer depends on the input video and the decoder options. By default
many videos will be coded using YUV420 which in that case means there are 3 different image planes (Y,U or V planes respectively).
To get a specific image plane; getFrameData can be used by passing the desired plane. This function will return a pointer 
to the start of the plane and the line size of each row in the image plane. The number of planes in each frame can be determined by:
~~~~
auto planes = frame->getNumberPlanes();
~~~~
By default when using hardware decoding the decoded frames will be output to system host memory. To keep the frames in device 
memory setup the decoder options such that:
~~~~
options.m_outputHost = false;
~~~~
In addition to just reading frames in sequence from start to finish, a stream object also supports seeking. Seeks can
be performed on either duration time stamps of specific frame numbers as follows:
~~~~
if (!stream->seek(2000)) {
    // Failed to seek to requested time stamp
}
~~~~