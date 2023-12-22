// MIT License
//
// Copyright(c) 2022-2023 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.h"

#include "utils.h"
#include <log.h>
#include <util.h>

#include "trackers.h"

#include <osc/OscReceivedElements.h>
#include <osc/OscPacketListener.h>
#include <ip/UdpSocket.h>

namespace openxr_api_layer {

    using namespace log;

    struct ETVREyeTracker : IEyeTracker, osc::OscPacketListener {
        // Steam Link allow us to choose between port 9000 (labeled VRChat) and 9015 ("custom"). We put ourselves under
        // "custom".
        ETVREyeTracker() : m_socket(IpEndpointName(IpEndpointName::ANY_ADDRESS, 9000), this) {
        }

        ~ETVREyeTracker() override {
            if (m_started) {
                m_socket.AsynchronousBreak();
                m_listeningThread.join();
            }
        }

        void start(XrSession session) override {
            m_listeningThread = std::thread([&]() { m_socket.Run(); });
            m_started = true;
        }

        void stop() override {
        }

        bool isGazeAvailable(XrTime time) const override {
            const auto now = std::chrono::high_resolution_clock::now();
            {
                std::unique_lock lock(m_mutex);
                return (now - m_lastReceivedTime).count() < 1'000'000'000;
            }
        }

        bool getGaze(XrTime time, XrVector3f& unitVector) override {
            if (!isGazeAvailable(time)) {
                return false;
            }

            std::unique_lock lock(m_mutex);
            unitVector = m_latestGaze;
            return true;
        }

        TrackerType getType() const override {
            return TrackerType::ETVR;
        }

        void ProcessMessage(const osc::ReceivedMessage& m, const IpEndpointName& remoteEndpoint) override {
            try {

                const auto now = std::chrono::high_resolution_clock::now();


                if (std::string_view(m.AddressPattern()) == "/avatar/parameters/EyesY") {

                    osc::ReceivedMessageArgumentStream args = m.ArgumentStream();
                    float ytemp;
                    args >> ytemp >> osc::EndMessage;

                    m_latestY = ytemp;

                    TraceLoggingWrite(g_traceProvider,
                                      "ETVREyeTracker_ProcessMessage",
                                      TLArg(xr::ToString(gaze).c_str(), "EyeTrackedGazePoint"));
                }
                if (std::string_view(m.AddressPattern()) == "/avatar/parameters/LeftEyeX") {

                    osc::ReceivedMessageArgumentStream args = m.ArgumentStream();
                    float xLtemp;
                    args >> xLtemp >> osc::EndMessage;

                    m_latestLeftX = xLtemp;

                    TraceLoggingWrite(g_traceProvider,
                                      "ETVREyeTracker_ProcessMessage",
                                      TLArg(xr::ToString(gaze).c_str(), "EyeTrackedGazePoint"));
                }
                if (std::string_view(m.AddressPattern()) == "/avatar/parameters/RightEyeX") {
                    
                    osc::ReceivedMessageArgumentStream args = m.ArgumentStream();
                    float xRtemp;
                    args >> xRtemp >> osc::EndMessage;

                    m_latestRightX = xRtemp;

                    TraceLoggingWrite(g_traceProvider,
                                      "ETVREyeTracker_ProcessMessage",
                                      TLArg(xr::ToString(gaze).c_str(), "EyeTrackedGazePoint"));
                }



                if (m_latestY && m_latestLeftX && m_latestRightX) {

                    float angleHorizontal = - (*m_latestRightX * M_PI / 4 + *m_latestLeftX * M_PI / 4) / 2;

                    float angleVertical = *m_latestY * M_PI / 4;

                    XrVector3f unitVector = {
                        sin(angleHorizontal) * cos(angleVertical),
                        sin(angleVertical),
                        -cos(angleHorizontal) * cos(angleVertical),
                    };

                    std::unique_lock lock(m_mutex);
                    m_latestGaze = unitVector;
                    m_lastReceivedTime = now;

                    m_latestLeftX.reset();
                    m_latestLeftX.reset();
                    m_latestRightX.reset();
                }


            } catch (osc::Exception& e) {
                TraceLoggingWrite(g_traceProvider, "ETVREyeTracker_ProcessMessage", TLArg(e.what(), "Error"));
            }
        }

        bool m_started{false};
        std::thread m_listeningThread;
        UdpListeningReceiveSocket m_socket;
        mutable std::mutex m_mutex;
        XrVector3f m_latestGaze{};
        std::optional<float> m_latestLeftX;
        std::optional<float> m_latestRightX;
        std::optional<float> m_latestY;
        std::chrono::high_resolution_clock::time_point m_lastReceivedTime{};
    };

    std::unique_ptr<IEyeTracker> createETVREyeTracker() {
        try {
            return std::make_unique<ETVREyeTracker>();
        } catch (...) {
            return {};
        }
    }

} // namespace openxr_api_layer
