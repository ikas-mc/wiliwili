#pragma once
#ifdef __PLAYER_WINRT__
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <future>
class HttpRandomAccessStream : public winrt::implements<HttpRandomAccessStream, 
    winrt::Windows::Storage::Streams::IRandomAccessStream,
    winrt::Windows::Storage::Streams::IRandomAccessStreamWithContentType,
    winrt::Windows::Storage::Streams::IInputStream, 
    winrt::no_weak_ref> {
public:
    HttpRandomAccessStream(const winrt::Windows::Web::Http::HttpClient& httpClient ,const std::string& url);
    ~HttpRandomAccessStream();
    uint64_t Size() const;
    void Size(uint64_t value);
    uint64_t Position() const;
    bool CanRead() const;
    bool CanWrite() const;
    winrt::hstring ContentType();
    winrt::Windows::Storage::Streams::IInputStream GetInputStreamAt(  uint64_t position) const;
    winrt::Windows::Storage::Streams::IOutputStream GetOutputStreamAt( uint64_t position) const;
    winrt::Windows::Storage::Streams::IRandomAccessStream CloneStream() const;
    void Seek(uint64_t position);
    winrt::Windows::Foundation::IAsyncOperationWithProgress<winrt::Windows::Storage::Streams::IBuffer, uint32_t> ReadAsync(winrt::Windows::Storage::Streams::IBuffer buffer, uint32_t count,  winrt::Windows::Storage::Streams::InputStreamOptions options);
    winrt::Windows::Foundation::IAsyncOperationWithProgress<uint32_t, uint32_t> WriteAsync(winrt::Windows::Storage::Streams::IBuffer buffer);
    winrt::Windows::Foundation::IAsyncOperation<bool> LoadAsync();

private:
    winrt::Windows::Foundation::Uri uri {nullptr};
    std::wstring m_contentType {L""};
    winrt::Windows::Web::Http::HttpClient m_httpClient{nullptr};
    uint64_t m_size              = 0;
    uint64_t m_requestedPosition = 0;
    winrt::Windows::Storage::Streams::IInputStream inputStream{ nullptr };
   
    winrt::Windows::Foundation::IAsyncAction SendHttpRequestAsync(_In_ uint64_t startPosition, _In_ uint32_t requestedSizeInBytes);
    };
#endif /*__PLAYER_WINRT__*/