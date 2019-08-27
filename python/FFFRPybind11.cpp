/**
 * Copyright 2019 Matthew Oliver
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "FFFREncoder.h"
#include "FFFrameReader.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

using namespace Ffr;

void bindFrameReader(pybind11::module& m)
{
    m.doc() = "Provides functions to decode and analyse input videos";

    pybind11::enum_<DecodeType>(m, "DecodeType", "")
        .value("Software", DecodeType::Software)
        .value("Cuda", DecodeType::Cuda);

    pybind11::class_<Resolution, std::shared_ptr<Resolution>>(m, "Resolution", "")
        .def(pybind11::init<uint32_t, uint32_t>())
        .def(pybind11::init([]() { return new Resolution(); }))
        .def(pybind11::init([](Resolution const& o) { return new Resolution(o); }))
        .def_readwrite("width", &Resolution::m_width)
        .def_readwrite("height", &Resolution::m_height)
        .def("assign", static_cast<Resolution& (Resolution::*)(const Resolution&)>(&Resolution::operator=), "",
            pybind11::return_value_policy::automatic, pybind11::arg("other"));

    pybind11::class_<Crop, std::shared_ptr<Crop>>(m, "Crop", "")
        .def(pybind11::init<uint32_t, uint32_t, uint32_t, uint32_t>())
        .def(pybind11::init([]() { return new Crop(); }))
        .def(pybind11::init([](Crop const& o) { return new Crop(o); }))
        .def_readwrite("top", &Crop::m_top)
        .def_readwrite("bottom", &Crop::m_bottom)
        .def_readwrite("left", &Crop::m_left)
        .def_readwrite("right", &Crop::m_right)
        .def("assign", static_cast<Crop& (Crop::*)(const Crop&)>(&Crop::operator=), "",
            pybind11::return_value_policy::automatic, pybind11::arg("other"));

    pybind11::enum_<PixelFormat>(m, "PixelFormat", "")
        .value("Auto", PixelFormat::Auto)
        .value("YUV420P", PixelFormat::YUV420P)
        .value("YUV422P", PixelFormat::YUV422P)
        .value("YUV444P", PixelFormat::YUV444P)
        .value("NV12", PixelFormat::NV12)
        .value("RGB8P", PixelFormat::RGB8P)
        .value("RGB32FP", PixelFormat::RGB32FP)
        .value("RGB8", PixelFormat::RGB8);

    pybind11::class_<DecoderOptions, std::shared_ptr<DecoderOptions>>(m, "DecoderOptions", "")
        .def(pybind11::init([]() { return new DecoderOptions(); }))
        .def(pybind11::init<DecodeType>(), pybind11::arg("type"))
        .def(pybind11::init([](DecoderOptions const& o) { return new DecoderOptions(o); }))
        .def_readwrite("type", &DecoderOptions::m_type)
        .def_readwrite("crop", &DecoderOptions::m_crop)
        .def_readwrite("scale", &DecoderOptions::m_scale)
        .def_readwrite("format", &DecoderOptions::m_format)
        .def_readwrite("bufferLength", &DecoderOptions::m_bufferLength)
        .def_readwrite("seekThreshold", &DecoderOptions::m_seekThreshold)
        .def_readwrite("noBufferFlush", &DecoderOptions::m_noBufferFlush)
        .def_readwrite("context", &DecoderOptions::m_context)
        .def_readwrite("device", &DecoderOptions::m_device)
        .def_readwrite("outputHost", &DecoderOptions::m_outputHost)
        .def("assign",
            static_cast<DecoderOptions& (DecoderOptions::*)(const DecoderOptions&)>(&DecoderOptions::operator=), "",
            pybind11::return_value_policy::automatic, pybind11::arg("other"))
        .def("__eq__", static_cast<bool (DecoderOptions::*)(const DecoderOptions&) const>(&DecoderOptions::operator==),
            "", pybind11::arg("other"))
        .def("__ne__", static_cast<bool (DecoderOptions::*)(const DecoderOptions&) const>(&DecoderOptions::operator!=),
            "", pybind11::arg("other"));

    pybind11::class_<Frame, std::shared_ptr<Frame>>(m, "Frame", "")
        .def("assign", static_cast<Frame& (Frame::*)(Frame&)>(&Frame::operator=), "",
            pybind11::return_value_policy::automatic, pybind11::arg("other"))
        .def("getTimeStamp", static_cast<int64_t (Frame::*)() const>(&Frame::getTimeStamp),
            "Gets global time stamp for frame.")
        .def("getFrameNumber", static_cast<int64_t (Frame::*)() const>(&Frame::getFrameNumber),
            "Gets picture sequence frame number.")
        .def("getFrameData",
            static_cast<std::pair<uint8_t* const, int32_t> (Frame::*)(uint32_t) const>(&Frame::getFrameData),
            "Gets frame data pointer for a specified image plane. The memory address of these frames will be in a memory space dependent on what settings where used when the parent stream was created. (e.g. cuda pointer if using nvdec etc.).",
            pybind11::arg("plane"))
        .def("getWidth", static_cast<uint32_t (Frame::*)() const>(&Frame::getWidth), "Gets the frame width.")
        .def("getHeight", static_cast<uint32_t (Frame::*)() const>(&Frame::getHeight), "Gets the frame height.")
        .def("getAspectRatio", static_cast<double (Frame::*)() const>(&Frame::getAspectRatio),
            "Gets the display aspect ratio of the video stream.")
        .def("getPixelFormat", static_cast<PixelFormat (Frame::*)() const>(&Frame::getPixelFormat),
            "Gets the pixel format of the frame data.")
        .def("getNumberPlanes", static_cast<int32_t (Frame::*)() const>(&Frame::getNumberPlanes),
            "Gets number of planes for an image of the specified pixel format.")
        .def("getDataType", static_cast<DecodeType (Frame::*)() const>(&Frame::getDataType),
            "Gets the type of memory used to store the image.");

    pybind11::class_<Stream, std::shared_ptr<Stream>>(m, "Stream", "")
        .def_static("getStream",
            static_cast<std::shared_ptr<Stream> (*)(const std::string&, const DecoderOptions&)>(&Stream::getStream),
            "Gets a stream from a file.", pybind11::arg("fileName"),
            pybind11::arg_v("options", DecoderOptions(), "DecoderOptions()"))
        .def("getWidth", static_cast<uint32_t (Stream::*)() const>(&Stream::getWidth),
            "Gets the width of the video stream.")
        .def("getHeight", static_cast<uint32_t (Stream::*)() const>(&Stream::getHeight),
            "Gets the height of the video stream.")
        .def("getAspectRatio", static_cast<double (Stream::*)() const>(&Stream::getAspectRatio),
            "Gets the display aspect ratio of the video stream.")
        .def("getPixelFormat", static_cast<PixelFormat (Stream::*)() const>(&Stream::getPixelFormat),
            "Gets the pixel format of the video stream.")
        .def("getTotalFrames", static_cast<int64_t (Stream::*)() const>(&Stream::getTotalFrames),
            "Gets total frames in the video stream.")
        .def("getDuration", static_cast<int64_t (Stream::*)() const>(&Stream::getDuration),
            "Gets the duration of the video stream in micro-seconds.")
        .def("getFrameRate", static_cast<double (Stream::*)() const>(&Stream::getFrameRate),
            "Gets the frame rate (fps) of the video stream.")
        .def("getFrameSize", static_cast<uint32_t (Stream::*)() const>(&Stream::getFrameSize),
            "Gets the storage size of each decoded frame in the video stream.")
        .def("getDecodeType", static_cast<DecodeType (Stream::*)() const>(&Stream::getDecodeType),
            "Gets the type of decoding used.")
        .def("peekNextFrame", static_cast<std::shared_ptr<Frame> (Stream::*)()>(&Stream::peekNextFrame),
            "Get the next frame in the stream without removing it from stream buffer.")
        .def("getNextFrame", static_cast<std::shared_ptr<Frame> (Stream::*)()>(&Stream::getNextFrame),
            "Gets the next frame in the stream and removes it from the buffer.")
        .def("getMaxFrames", static_cast<uint32_t (Stream::*)()>(&Stream::getMaxFrames),
            "Gets maximum frames that can exist at a time.")
        .def("isEndOfFile", static_cast<bool (Stream::*)() const>(&Stream::isEndOfFile),
            "Query if the stream has reached end of input file.")
        .def("seek", static_cast<bool (Stream::*)(int64_t)>(&Stream::seek),
            "Seeks the stream to the given time stamp. If timestamp does not exactly match a frame hen the timestamp rounded to the nearest frame is used instead.",
            pybind11::arg("timeStamp"))
        .def("seekFrame", static_cast<bool (Stream::*)(int64_t)>(&Stream::seekFrame),
            "Seeks the stream to the given frame number.", pybind11::arg("frame"))
        .def("frameToTime", static_cast<int64_t (Stream::*)(int64_t) const>(&Stream::frameToTime),
            "Convert a zero-based frame number to time value represented in microseconds AV_TIME_BASE).",
            pybind11::arg("frame"))
        .def("timeToFrame", static_cast<int64_t (Stream::*)(int64_t) const>(&Stream::timeToFrame),
            "Convert a time value represented in microseconds (AV_TIME_BASE) to a zero-based frame number.",
            pybind11::arg("time"));

    pybind11::enum_<EncodeType>(m, "EncodeType", "").value("h264", EncodeType::h264).value("h265", EncodeType::h265);

    {
        pybind11::class_<EncoderOptions, std::shared_ptr<EncoderOptions>> cl(m, "EncoderOptions", "");
        cl.def(pybind11::init([]() { return new EncoderOptions(); }));
        cl.def(pybind11::init([](EncoderOptions const& o) { return new EncoderOptions(o); }));
        pybind11::enum_<EncoderOptions::Preset>(cl, "Preset", "")
            .value("Ultrafast", EncoderOptions::Preset::Ultrafast)
            .value("Superfast", EncoderOptions::Preset::Superfast)
            .value("Veryfast", EncoderOptions::Preset::Veryfast)
            .value("Faster", EncoderOptions::Preset::Faster)
            .value("Fast", EncoderOptions::Preset::Fast)
            .value("Medium", EncoderOptions::Preset::Medium)
            .value("Slow", EncoderOptions::Preset::Slow)
            .value("Slower", EncoderOptions::Preset::Slower)
            .value("Veryslow", EncoderOptions::Preset::Veryslow)
            .value("Placebo", EncoderOptions::Preset::Placebo);
        cl.def_readwrite("type", &EncoderOptions::m_type);
        cl.def_readwrite("quality", &EncoderOptions::m_quality);
        cl.def_readwrite("preset", &EncoderOptions::m_preset);
        cl.def_readwrite("numThreads", &EncoderOptions::m_numThreads);
        cl.def_readwrite("gopSize", &EncoderOptions::m_gopSize);
        cl.def("assign",
            static_cast<EncoderOptions& (EncoderOptions::*)(const EncoderOptions&)>(&EncoderOptions::operator=), "",
            pybind11::return_value_policy::automatic, pybind11::arg("other"));
        cl.def("__eq__",
            static_cast<bool (EncoderOptions::*)(const EncoderOptions&) const>(&EncoderOptions::operator==), "",
            pybind11::arg("other"));
        cl.def("__ne__",
            static_cast<bool (EncoderOptions::*)(const EncoderOptions&) const>(&EncoderOptions::operator!=), "",
            pybind11::arg("other"));
    }

    pybind11::class_<Encoder, std::shared_ptr<Encoder>>(m, "Encoder", "")
        .def_static("encodeStream",
            static_cast<bool (*)(const std::string&, const std::shared_ptr<Stream>&, const EncoderOptions&)>(
                &Encoder::encodeStream),
            "Encodes a stream to a file", pybind11::arg("fileName"), pybind11::arg("stream"),
            pybind11::arg_v("options", EncoderOptions(), "EncoderOptions()"));
}

PYBIND11_MODULE(pyFrameReader, m)
{
    bindFrameReader(m);
}
