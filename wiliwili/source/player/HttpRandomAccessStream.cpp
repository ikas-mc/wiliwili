//
// Created by ikas on 2023/12/26.
//
#ifdef __PLAYER_WINRT__
#include "player/HttpRandomAccessStream.h"
#include <winrt/base.h>
#include <winrt/windows.web.http.headers.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Foundation.Collections.h>
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Security::Cryptography;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Web::Http;
using namespace winrt::Windows::Web::Http::Headers;
using namespace winrt::Windows::Web::Http::Filters;

HttpRandomAccessStream::HttpRandomAccessStream(const std::string& url) : uri(Uri{ winrt::to_hstring(url) }) {
	//TODO donot create here,ikas
	m_httpClient = HttpClient();

	m_httpClient.DefaultRequestHeaders().Append(L"Connection", L"Keep-Alive");
	m_httpClient.DefaultRequestHeaders().UserAgent().Append(winrt::Windows::Web::Http::Headers::HttpProductInfoHeaderValue::Parse(L"bilibili"));
	m_httpClient.DefaultRequestHeaders().Referer(winrt::Windows::Foundation::Uri(L"https://www.bilibili.com"));
}

HttpRandomAccessStream::~HttpRandomAccessStream() {
	m_httpClient.Close();
}

uint64_t HttpRandomAccessStream::Size() const {
	return m_size;
}

void HttpRandomAccessStream::Size(uint64_t value) {
}

uint64_t HttpRandomAccessStream::Position() const {
	return m_requestedPosition;
}

bool HttpRandomAccessStream::CanRead() const {
	return true;
}

bool HttpRandomAccessStream::CanWrite() const {
	return false;
}

IInputStream HttpRandomAccessStream::GetInputStreamAt(uint64_t position) const {
	winrt::throw_hresult(winrt::hresult(-1));

}

IOutputStream HttpRandomAccessStream::GetOutputStreamAt(uint64_t position) const {
	winrt::throw_hresult(winrt::hresult(-1));
}

IRandomAccessStream HttpRandomAccessStream::CloneStream() const {
	winrt::throw_hresult(winrt::hresult(-1));
}

void HttpRandomAccessStream::Seek(uint64_t position) {
	m_requestedPosition = position;
	inputStream = nullptr;
}

IAsyncOperationWithProgress<IBuffer, uint32_t> HttpRandomAccessStream::ReadAsync(IBuffer buffer, uint32_t count, InputStreamOptions options) {
	if (!inputStream) {
		co_await SendHttpRequestAsync(m_requestedPosition, -1);
	}

	if (m_requestedPosition > m_size - 1) {
		co_return buffer;
	}

	if (m_requestedPosition + count > m_size) {
		count = m_size - m_requestedPosition;
	}

	winrt::Windows::Storage::Streams::IBuffer data= co_await this->inputStream.ReadAsync(buffer, count, options);
	m_requestedPosition += data.Length();
	co_return data;
}


IAsyncOperationWithProgress<uint32_t, uint32_t> HttpRandomAccessStream::WriteAsync(winrt::Windows::Storage::Streams::IBuffer buffer) {
	winrt::throw_hresult(winrt::hresult(-1));
}

winrt::hstring HttpRandomAccessStream::ContentType() {
	return winrt::hstring{ m_contentType };
}

// this function will issue a HEAD request to determine the size of the file and the redirect URI
IAsyncOperation<bool> HttpRandomAccessStream::LoadAsync() {
	m_size = 0;

	HttpRequestMessage request(HttpMethod::Head(), uri);
	HttpResponseMessage response = co_await m_httpClient.SendRequestAsync(request, HttpCompletionOption::ResponseHeadersRead);
	switch (response.StatusCode()) {
		case HttpStatusCode::Ok:
		{
			if (response.Content().Headers().HasKey(L"Content-Length")) {
				std::wstring contentLength(response.Content().Headers().Lookup(L"Content-Length"));
				m_size = std::stoll(contentLength);
			}
			m_contentType = response.Content().Headers().HasKey(L"Content-Type") ? response.Content().Headers().Lookup(L"Content-Type") : L"";
			break;
		}
		case HttpStatusCode::BadRequest:
		case HttpStatusCode::MethodNotAllowed:
		case HttpStatusCode::NotImplemented:
		case HttpStatusCode::NotFound:
		case HttpStatusCode::Forbidden:
		{
			break;
		}
		//case HttpStatusCode::TooManyRequests:
		//case HttpStatusCode::ServiceUnavailable:
		default: {
			//throw;
		}
	}

	if (m_size < 1) {
		HttpRequestMessage request2(HttpMethod::Get(), uri);
		request2.Headers().Append(L"Range", L"bytes=0-0");
		HttpResponseMessage response2 = co_await m_httpClient.SendRequestAsync(request2, HttpCompletionOption::ResponseHeadersRead);
		switch (response2.StatusCode()) {
		    case HttpStatusCode::PartialContent:
			case HttpStatusCode::Ok:
			{
				if (response2.Content().Headers().HasKey(L"Content-Range"))
				{
					std::wstring contentRange{ response2.Content().Headers().Lookup(L"Content-Range") };
					std::wstring length = contentRange.substr(contentRange.find(L"/") + 1);
					m_size = (length == L"*") ? 0 : std::stoll(length);
				}
				break;
			}
			default: {
				//throw;
			}
		}
	}
	
	co_return m_size > 0;
}

IAsyncAction HttpRandomAccessStream::SendHttpRequestAsync(_In_ uint64_t startPosition, _In_ uint32_t requestedSizeInBytes) {
		HttpRequestMessage request(HttpMethod::Get(), uri);
		request.Headers().Append(L"Range", std::format(L"bytes={}-", startPosition));

		HttpResponseMessage  response = co_await m_httpClient.SendRequestAsync(request, HttpCompletionOption::ResponseHeadersRead);
		HttpContentHeaderCollection contentHeaders = response.Content().Headers();

		switch (response.StatusCode()) {
			case HttpStatusCode::Ok:
			case HttpStatusCode::PartialContent: 
			{
				inputStream = co_await response.Content().ReadAsInputStreamAsync();
				break;
			}
			default: {
				//throw;
			}
		}
}
#endif /*__PLAYER_WINRT__*/