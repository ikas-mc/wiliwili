//
// Created by fang on 2022/8/12.
//
#ifdef __PLAYER_WINRT__
#include <cstdlib>
#include <clocale>
#include "view/mpv_core.hpp"
#include "utils/config_helper.hpp"
#include "utils/number_helper.hpp"
#include <pystring.h>

#include <ppltasks.h>
#include <winrt/windows.system.h>
#include <winrt/windows.applicationmodel.core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Streaming.Adaptive.h>
#include <winrt/Windows.Networking.BackgroundTransfer.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Foundation.Collections.h>
#include "player/HttpRandomAccessStream.h"
#include <nanovg_d3d11.h>

static inline void check_error(int status) {
	if (status < 0) {
		brls::Logger::error("MPV ERROR ====> {}", mpv_error_string(status));
	}
}

static inline int MulDiv2(int nNumber, int nNumerator, int nDenominator)
{
	return (nNumber * nNumerator + nDenominator / 2) / nDenominator;
}

static inline float aspectConverter(const std::string& value) {
	try {
		if (value.empty()) {
			return -1;
		}
		else if (pystring::count(value, ":")) {
			// 比例模式
			auto num = pystring::split(value, ":");
			if (num.size() != 2) return -1;
			return std::stof(num[0]) / std::stof(num[1]);
		}
		else {
			// 纯数字
			return std::stof(value);
		}
	}
	catch (const std::exception& e) {
		return -1;
	}
}

void MPVCore::on_update(void* self) {
	brls::sync([]() {

		});
}

void MPVCore::on_wakeup(void* self) {
	brls::sync([]() {
		MPVCore::instance().eventMainLoop();
		});
}

MPVCore::MPVCore() {
	this->init();
	// Destroy mpv when application exit
	brls::Application::getExitDoneEvent()->subscribe([this]() {
		this->clean();
		});
}

void MPVCore::init() {
	setlocale(LC_NUMERIC, "C");

	httpClient = winrt::Windows::Web::Http::HttpClient();
	httpClient.DefaultRequestHeaders().UserAgent().Append(winrt::Windows::Web::Http::Headers::HttpProductInfoHeaderValue::Parse(L"bilibili"));
	httpClient.DefaultRequestHeaders().Referer(winrt::Windows::Foundation::Uri(L"https://www.bilibili.com"));

	brls::Logger::info("use winrt MediaPlayer");

	mediaPlayer = winrt::Windows::Media::Playback::MediaPlayer();
	mediaPlayer.IsVideoFrameServerEnabled(true);
	mediaPlayer.VideoFrameAvailable({ this, &MPVCore::OnVideoFrameAvailable });

	mediaPlayer.AudioCategory(winrt::Windows::Media::Playback::MediaPlayerAudioCategory::Movie);
	mediaPlayer.VolumeChanged({ this,&MPVCore::PlayerVolumeChanged });
	mediaPlayer.PlaybackSession().PositionChanged({ this, &MPVCore::PositionChanged });
	mediaPlayer.PlaybackSession().PlaybackStateChanged({ this, &MPVCore::PlaybackStateChanged });
	mediaPlayer.PlaybackSession().BufferingStarted({ this, &MPVCore::BufferingStarted });
	mediaPlayer.PlaybackSession().BufferingEnded({ this, &MPVCore::BufferingEnded });
	mediaPlayer.MediaEnded({ this,&MPVCore::MediaEnded });

	if (MPVCore::VIDEO_ASPECT != "-1") {
		video_aspect = aspectConverter(MPVCore::VIDEO_ASPECT);
	}

	setVolume(MPVCore::VIDEO_VOLUME);

	focusSubscription =
		brls::Application::getWindowFocusChangedEvent()->subscribe(
			[this](bool focus) {
				static bool playing = false;
				static std::chrono::system_clock::time_point sleepTime{};
				// save current AUTO_PLAY value to autoPlay
				static bool autoPlay = AUTO_PLAY;
				if (focus) {
					// restore AUTO_PLAY
					AUTO_PLAY = autoPlay;
					// application is on top
					auto timeNow = std::chrono::system_clock::now();
					if (playing &&
						timeNow < (sleepTime + std::chrono::seconds(120))) {
						resume();
					}
				}
				else {
					// application is sleep, save the current state
					playing = isPlaying();
					sleepTime = std::chrono::system_clock::now();
					pause();
					// do not automatically play video
					AUTO_PLAY = false;
				}
			});

	brls::Application::getExitEvent()->subscribe(
		[]() { disableDimming(false); });

	this->initializeVideo();
}

MPVCore::~MPVCore() {
	this->mediaPlayer = nullptr;
};

void MPVCore::clean() {
	brls::Application::getWindowFocusChangedEvent()->unsubscribe(focusSubscription);

	brls::Logger::info("uninitialize Video");
	this->uninitializeVideo();

	this->videoSource = nullptr;
}

void MPVCore::restart() {
	this->clean();
	this->init();
}

void MPVCore::uninitializeVideo() {

}

void MPVCore::initializeVideo() {
}

void MPVCore::setFrameSize(brls::Rect r) {
	//rect = r;
	//if (isnan(r.getWidth()) || isnan(r.getHeight())) return;
}

bool MPVCore::isValid() { return true; }

void MPVCore::draw(brls::Rect area, float alpha) {
	//brls::Logger::debug("MPVCore draw");
	//if (!(this->rect == area)) {
	//	setFrameSize(area);
	//}

	auto* vg = brls::Application::getNVGContext();

	//NVGparams* params = nvgInternalParams(vg);
	//D3DNVGcontext* params->userPtr;

	if (mediaPlayer.PlaybackSession().PlaybackState() == winrt::Windows::Media::Playback::MediaPlaybackState::Playing) {
		//copy frame
		//TODO copy in OnVideoFrameAvailable, @ikas
		concurrency::create_task([&] {
			//brls::Logger::debug("MPVCore copy image");
			//create frame
			auto videoWidth = mediaPlayer.PlaybackSession().NaturalVideoWidth();
			auto videoHeight = mediaPlayer.PlaybackSession().NaturalVideoHeight();

			auto videoFrame = winrt::Windows::Media::VideoFrame::CreateAsDirect3D11SurfaceBacked(winrt::Windows::Graphics::DirectX::DirectXPixelFormat::R8G8B8A8UIntNormalized, videoWidth, videoHeight);
			mediaPlayer.CopyFrameToVideoSurface(videoFrame.Direct3DSurface());

			auto softwareBitmap = winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromSurfaceAsync(videoFrame.Direct3DSurface()).get();
			auto buffer = softwareBitmap.LockBuffer(winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode::Read);
			uint8_t* pixels = buffer.CreateReference().data();

			//update frame data
			auto currentFrameWidth = softwareBitmap.PixelWidth();
			auto currentFrameHeight = softwareBitmap.PixelHeight();
			if (!nvg_image || currentFrameWidth != lastFrameWidth || currentFrameHeight != lastFrameHeight) {
				lastFrameWidth = currentFrameWidth;
				lastFrameHeight = currentFrameHeight;
				auto mpvImageFlags = NVG_IMAGE_STREAMING | NVG_IMAGE_COPY_SWAP;
				if (nvg_image) {
					nvgDeleteImage(vg, nvg_image);
					nvg_image = 0;
				}
				nvg_image = nvgCreateImageRGBA(vg, currentFrameWidth, currentFrameHeight, mpvImageFlags, (const unsigned char*)pixels);

			}
			else {
				nvgUpdateImage(vg, nvg_image, (const unsigned char*)pixels);
			}

			}).wait();
	}
	else {
		if (lastFrameWidth == 0 && lastFrameHeight ==0 && nvg_image) {
			nvgDeleteImage(vg, nvg_image);
			nvg_image = 0;
		}
	}

	// draw black background
	//brls::Logger::debug("MPVCore draw background");
	nvgBeginPath(vg);
	NVGcolor bg{};
	bg.a = alpha;
	nvgFillColor(vg, bg);
	nvgRect(vg, area.getMinX(), area.getMinY(), area.getWidth(), area.getHeight());
	nvgFill(vg);

	if (!nvg_image) {
		return;
	}
	//draw image
	//brls::Logger::debug("MPVCore draw image ");
	//target  size
	int targetWidth;
	int targetHeight;
	int viewWidth = area.getWidth();
	int viewHeight = area.getHeight();
	if (MulDiv2(lastFrameWidth, viewHeight, lastFrameHeight) <= viewWidth) {
		targetWidth = MulDiv2(viewHeight, lastFrameWidth, lastFrameHeight);
		targetHeight = viewHeight;
	}
	else {
		targetWidth = viewWidth;
		targetHeight = MulDiv2(viewWidth, lastFrameHeight, lastFrameWidth);
	}
	auto targetX = area.getMinX() + ((viewWidth - targetWidth) / 2);
	auto targetY = area.getMinY() + ((viewHeight - targetHeight) / 2);

	nvgBeginPath(vg);
	nvgRect(vg, targetX, targetY, targetWidth, targetHeight);
	nvgFillPaint(vg, nvgImagePattern(vg, targetX, targetY, targetWidth, targetHeight, 0, nvg_image, alpha));
	nvgFill(vg);
}

MPVEvent* MPVCore::getEvent() { return &this->mpvCoreEvent; }

MPVCustomEvent* MPVCore::getCustomEvent() { return &this->mpvCoreCustomEvent; }

std::string MPVCore::getCacheSpeed() const {
	if (cache_speed >> 20 > 0) {
		return fmt::format("{:.2f} MB/s", (cache_speed >> 10) / 1024.0f);
	}
	else if (cache_speed >> 10 > 0) {
		return fmt::format("{:.2f} KB/s", cache_speed / 1024.0f);
	}
	else {
		return fmt::format("{} B/s", cache_speed);
	}
}

void MPVCore::eventMainLoop() {

}

void MPVCore::reset() {
	brls::Logger::debug("MPVCore::reset");
	mpvCoreEvent.fire(MpvEventEnum::RESET);
	this->percent_pos = 0;
	this->duration = 0;  // second
	this->cache_speed = 0;  // Bps
	this->playback_time = 0;
	this->video_progress = 0;
	this->mpv_error_code = 0;

	mediaPlayer.Source(nullptr);
	lastFrameWidth = 0;
	lastFrameHeight = 0;

	// 软硬解切换后应该手动设置一次渲染尺寸
	// 切换视频前设置渲染尺寸可以顺便将上一条视频的最后一帧画面清空
	//setFrameSize(rect);
}

void MPVCore::OnVideoFrameAvailable(winrt::Windows::Media::Playback::MediaPlayer sender,
	winrt::Windows::Foundation::IInspectable arg) {
	//this->video_playing = true;
}

void MPVCore::setDashUrl(int start, int end,
	std::string videoUrl, std::string videoIndexRange, std::string videoInitRange,
	std::string audioUrl, std::string audioIndexRange, std::string audioInitRange
) {
	sourceType = 2;
	concurrency::create_task([&] {
		auto mpd = std::format(R"(﻿<MPD xmlns="urn:mpeg:DASH:schema:MPD:2011" profiles="urn:mpeg:dash:profile:isoff-on-demand:2011" type="static">
    <Period start="PT0S">
        <AdaptationSet>
            <ContentComponent contentType="video" id="1" />
            <Representation bandwidth="1024" mimeType="video/mp4" id="1" startWithSap="1">
                <SegmentBase indexRange="{}">
                    <Initialization range="{}" />
                </SegmentBase>
            </Representation>
        </AdaptationSet>
        <AdaptationSet>
            <ContentComponent contentType="audio" id="1" />
            <Representation bandwidth="1024" mimeType="audio/mp4" id="1">
                <SegmentBase indexRange="{}">
                    <Initialization range="{}" />
                </SegmentBase>
            </Representation>
        </AdaptationSet>
    </Period>
</MPD>)",  videoIndexRange, videoInitRange,  audioIndexRange, audioInitRange);

		winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
		winrt::Windows::Storage::Streams::DataWriter dataWriter{ stream };
		dataWriter.UnicodeEncoding(winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8);
		dataWriter.WriteString(winrt::to_hstring(mpd));
		dataWriter.StoreAsync().get();
		dataWriter.DetachStream();
		dataWriter.Close();
		stream.Seek(0);

		lastVideoUri = winrt::Windows::Foundation::Uri(winrt::to_hstring(videoUrl));
		lastAudioUri = winrt::Windows::Foundation::Uri(winrt::to_hstring(audioUrl));

		auto source = winrt::Windows::Media::Streaming::Adaptive::AdaptiveMediaSource::CreateFromStreamAsync(stream, lastVideoUri, winrt::to_hstring("application/dash+xml"), httpClient).get();

		auto status = source.Status();
		if (winrt::Windows::Media::Streaming::Adaptive::AdaptiveMediaSourceCreationStatus::Success == status) {
			source.MediaSource().AdvancedSettings().AllSegmentsIndependent(true);
			
			source.MediaSource().DownloadRequested([this](
				winrt::Windows::Media::Streaming::Adaptive::AdaptiveMediaSource, 
				winrt::Windows::Media::Streaming::Adaptive::AdaptiveMediaSourceDownloadRequestedEventArgs const& args
				){
				if (args.ResourceContentType() == L"video/mp4")
				{
					args.Result().ResourceUri(lastVideoUri);
				}
				else if (args.ResourceContentType() == L"audio/mp4")
				{
					args.Result().ResourceUri(lastAudioUri);
				}
			});
			
			mediaPlayer.Source(winrt::Windows::Media::Core::MediaSource::CreateFromAdaptiveMediaSource(source.MediaSource()));
		}

		mediaPlayer.Play();
		
	}).wait();
}

void MPVCore::setUrl(const std::string& url, const std::string& extra,
	const std::string& method) {
	brls::Logger::debug("{} Url: {}, extra: {}", method, url, extra);

	if (method != "replace") {
		return;
	}

	//TODO need more video info 
	winrt::Windows::Foundation::Uri videoUri{ winrt::to_hstring(url) };
	if (videoUri.Extension() == L".m3u8") {
		sourceType = 3;
	}
	else {
		sourceType = 1;
	}

	concurrency::create_task([&] {
		if (sourceType == 3) {
			auto source = winrt::Windows::Media::Streaming::Adaptive::AdaptiveMediaSource::CreateFromUriAsync(videoUri, httpClient).get();
			if (winrt::Windows::Media::Streaming::Adaptive::AdaptiveMediaSourceCreationStatus::Success == source.Status()) {
				mediaPlayer.Source(winrt::Windows::Media::Core::MediaSource::CreateFromAdaptiveMediaSource(source.MediaSource()));
				mediaPlayer.Play();
			}
		}
		else {
			auto stream = winrt::make_self<HttpRandomAccessStream>(httpClient, url);
			if (stream->LoadAsync().get()) {
				auto videoStream = stream.as<winrt::Windows::Storage::Streams::IRandomAccessStream>();
			    mediaPlayer.SetStreamSource(videoStream);
				videoSource = stream;
				mediaPlayer.Play();
			}
		}
		}).wait();
}

void MPVCore::setBackupUrl(const std::string& url, const std::string& extra) {
	//this->setUrl(url, extra, "append");
}

void MPVCore::setVolume(int64_t value) {
	if (value < 0 || value > 100) {
		return;
	};
	MPVCore::VIDEO_VOLUME = (int)value;
	mediaPlayer.Volume(value * 0.01);
}

void MPVCore::setVolume(const std::string& value) {
	MPVCore::VIDEO_VOLUME = std::stoi(value);
	mediaPlayer.Volume(MPVCore::VIDEO_VOLUME * 0.01);
}

int64_t MPVCore::getVolume() const {
	return (int64_t)(mediaPlayer.Volume() * 100);
}

void MPVCore::resume() {
	mediaPlayer.Play();
}

void MPVCore::pause() {
	mediaPlayer.Pause();
}

void MPVCore::stop() {
	this->pause();
	video_stopped = true;
	mediaPlayer.Source(nullptr);
}

void MPVCore::seek(int64_t p) {
	if (p < duration) {
		mediaPlayer.PlaybackSession().Position(std::chrono::seconds(static_cast<int64_t>(p)));
	}
}

void MPVCore::seek(const std::string& p) {
	auto position = std::stoll(p);
	seek(p);
}

void MPVCore::seekRelative(int64_t p) {
	auto position = static_cast<int64_t>(playback_time + p);
	seek(position);
}

void MPVCore::seekPercent(double p) {
	auto position = static_cast<int64_t>(duration * p);
	seek(position);
}

bool MPVCore::isStopped() const { return video_stopped; }

bool MPVCore::isPlaying() const { return video_playing; }

bool MPVCore::isPaused() const { return video_paused; }

double MPVCore::getSpeed() const { return video_speed; }

void MPVCore::setSpeed(double value) {
	mediaPlayer.PlaybackRate(value);
}

void MPVCore::setAspect(const std::string& value) {
	MPVCore::VIDEO_ASPECT = value;
	//video_aspect          = aspectConverter(MPVCore::VIDEO_ASPECT);
	//TODO 
}

void MPVCore::setBrightness(int value) {
	if (value < -100) value = -100;
	if (value > 100) value = 100;
}

void MPVCore::setContrast(int value) {
	if (value < -100) value = -100;
	if (value > 100) value = 100;
}

void MPVCore::setSaturation(int value) {
	if (value < -100) value = -100;
	if (value > 100) value = 100;

}

void MPVCore::setGamma(int value) {
	if (value < -100) value = -100;
	if (value > 100) value = 100;
}

void MPVCore::setHue(int value) {
	if (value < -100) value = -100;
	if (value > 100) value = 100;
}

int MPVCore::getBrightness() const { return video_brightness; }

int MPVCore::getContrast() const { return video_contrast; }

int MPVCore::getSaturation() const { return video_saturation; }

int MPVCore::getGamma() const { return video_gamma; }

int MPVCore::getHue() const { return video_hue; }

std::string MPVCore::getString(const std::string& key) {
	return "";
}

double MPVCore::getDouble(const std::string& key) {
	double value = 0;
	return value;
}

int64_t MPVCore::getInt(const std::string& key) {
	int64_t value = 0;
	return value;
}

std::unordered_map<std::string, mpv_node> MPVCore::getNodeMap(
	const std::string& key) {
	std::unordered_map<std::string, mpv_node> nodeMap;
	return nodeMap;
}

double MPVCore::getPlaybackTime() const { return playback_time; }

void MPVCore::disableDimming(bool disable) {
	brls::Logger::info("disableDimming: {}", disable);
	brls::Application::getPlatform()->disableScreenDimming(
		disable, "Playing video", APPVersion::getPackageName());
	static bool deactivationAvailable =
		ProgramConfig::instance().getSettingItem(SettingItem::DEACTIVATED_TIME,
			0) > 0;
	if (deactivationAvailable) {
		brls::Application::setAutomaticDeactivation(!disable);
	}
}

void MPVCore::setShader(const std::string& profile, const std::string& shaders,
	bool showHint) {
	brls::Logger::info("Set shader [{}]: {}", profile, shaders);
	if (shaders.empty()) return;

	if (showHint) showOsdText(profile);
}

void MPVCore::clearShader(bool showHint) {
	brls::Logger::info("Clear shader");
	if (showHint) showOsdText("Clear shader");
}

void MPVCore::showOsdText(const std::string& value, int d) {
}

void MPVCore::PlayerVolumeChanged(const winrt::Windows::Media::Playback::MediaPlayer& player, const winrt::Windows::Foundation::IInspectable& value) {
	mpvCoreEvent.fire(MpvEventEnum::VIDEO_VOLUME_CHANGE);
}

void MPVCore::PlaybackStateChanged(const winrt::Windows::Media::Playback::MediaPlaybackSession& session,
	const winrt::Windows::Foundation::IInspectable& value) {
	if (session.PlaybackState() == winrt::Windows::Media::Playback::MediaPlaybackState::Playing) {
		video_playing = true;
		video_paused = false;
		video_stopped = false;
		brls::async([this] {
			mpvCoreEvent.fire(MpvEventEnum::MPV_RESUME);
			mpvCoreEvent.fire(MpvEventEnum::MPV_IDLE);
			disableDimming(true);
		});

	}
	else if (session.PlaybackState() == winrt::Windows::Media::Playback::MediaPlaybackState::Buffering) {
		//video_playing = true;
		//mpvCoreEvent.fire(MpvEventEnum::LOADING_START);
		//disableDimming(false);
	}
	else if (session.PlaybackState() == winrt::Windows::Media::Playback::MediaPlaybackState::Opening) {
		//video_playing = true;
		//mpvCoreEvent.fire(MpvEventEnum::LOADING_START);
		//disableDimming(false);
	}
	else if (session.PlaybackState() == winrt::Windows::Media::Playback::MediaPlaybackState::Paused) {
		video_playing = false;
		video_paused = true;
		//video_stopped = false;
		brls::async([this] {
			disableDimming(false);
			mpvCoreEvent.fire(MpvEventEnum::MPV_PAUSE);
			mpvCoreEvent.fire(MpvEventEnum::MPV_IDLE);
		});
	}
}

void MPVCore::PositionChanged(const winrt::Windows::Media::Playback::MediaPlaybackSession& session,
	const winrt::Windows::Foundation::IInspectable& value) {
	this->playback_time = std::chrono::duration_cast<std::chrono::seconds>(session.Position()).count();
	auto newDuration = std::chrono::duration_cast<std::chrono::seconds>(session.NaturalDuration()).count();

	if (newDuration != this->duration) {
		this->duration = newDuration;
		mpvCoreEvent.fire(MpvEventEnum::UPDATE_DURATION);
	}

	if (std::abs(this->video_progress - this->playback_time) >=1) {
		this->video_speed = session.PlaybackRate();
		this->video_progress = (int64_t)this->playback_time;
		mpvCoreEvent.fire(MpvEventEnum::UPDATE_PROGRESS);
	}
}

void MPVCore::BufferingStarted(const winrt::Windows::Media::Playback::MediaPlaybackSession& session,
	const winrt::Windows::Foundation::IInspectable& value) {
	// event 8: 文件预加载结束，准备解码
	//mpvCoreEvent.fire(MpvEventEnum::MPV_LOADED);
	// event 6: 开始加载文件
	brls::Logger::info("========> MPV_EVENT_START_FILE");
	// show osd for a really long time
	//mpvCoreEvent.fire(MpvEventEnum::START_FILE);
	brls::async([this] {
		mpvCoreEvent.fire(MpvEventEnum::LOADING_START);
	});

}

void MPVCore::BufferingEnded(const winrt::Windows::Media::Playback::MediaPlaybackSession& session,
	const winrt::Windows::Foundation::IInspectable& value) {
	brls::Logger::info("========> MPV_EVENT_PLAYBACK_RESTART");
	//mpvCoreEvent.fire(MpvEventEnum::LOADING_END);
}

void MPVCore::MediaEnded(winrt::Windows::Media::Playback::MediaPlayer, winrt::Windows::Foundation::IInspectable const& value) {
	// event 7: 文件播放结束
	brls::Logger::info("========> MPV_STOP");
	video_stopped = true;

	brls::delay(200, [this] {
		mpvCoreEvent.fire(MpvEventEnum::MPV_STOP);
		mpvCoreEvent.fire(MpvEventEnum::END_OF_FILE);
		//TODO
		});
}

#endif /*__PLAYER_WINRT__*/