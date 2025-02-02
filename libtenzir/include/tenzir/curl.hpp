//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2023 The Tenzir Contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "tenzir/fwd.hpp"

#include "tenzir/data.hpp"
#include "tenzir/generator.hpp"

#include <caf/error.hpp>
#include <caf/expected.hpp>
#include <curl/curl.h>

#include <chrono>
#include <string>

namespace tenzir::curl {

/// A list of strings, corresponding to a `curl_slist`.
class slist {
  friend class easy;

public:
  slist() = default;

  /// Appends a string to the list.
  /// @param str The string to append.
  /// @pre *str* must be NULL-terminated.
  auto append(std::string_view str) -> void;

  /// Iterates over the list items.
  /// @returns a generator over the strings.
  auto items() const -> generator<std::string_view>;

private:
  struct curl_slist_deleter {
    auto operator()(curl_slist* ptr) const noexcept -> void {
      if (ptr)
        curl_slist_free_all(ptr);
    }
  };

  std::unique_ptr<curl_slist, curl_slist_deleter> slist_;
};

/// Function for `CURLOPT_WRITEFUNCTION`.
using write_callback = std::function<void(std::span<const std::byte>)>;

/// Function for `CURLOPT_READFUNCTION`.
/// The read callback gets called as soon as the handle needs to read data. It
/// takes as argument a buffer that can be written to. The return value
/// represents the number of bytes written. Returning 0 signals end-of-file to
/// the library and causes it to stop the current transfer.
using read_callback = std::function<size_t(std::span<std::byte>)>;

/// Write callback that assumes `user_data` to be a `write_callback*`.
auto on_write(void* ptr, size_t size, size_t nmemb, void* user_data) -> size_t;

/// Read callback that assumes `user_data` to be a `read_callback*`.
auto on_read(char* buffer, size_t size, size_t nitems, void* user_data)
  -> size_t;

class mime;

/// A single transfer, corresponding to a cURL "easy" handle.
class easy {
  friend class mime;
  friend class multi;

public:
  /// The `CURLcode` enum.
  enum class code {
    ok = CURLE_OK,
    unsupported_protocol = CURLE_UNSUPPORTED_PROTOCOL,
    failed_init = CURLE_FAILED_INIT,
    url_malformat = CURLE_URL_MALFORMAT,
    not_built_in = CURLE_NOT_BUILT_IN,
    couldnt_resolve_proxy = CURLE_COULDNT_RESOLVE_PROXY,
    couldnt_resolve_host = CURLE_COULDNT_RESOLVE_HOST,
    couldnt_connect = CURLE_COULDNT_CONNECT,
    weird_server_reply = CURLE_WEIRD_SERVER_REPLY,
    remote_access_denied = CURLE_REMOTE_ACCESS_DENIED,
    ftp_accept_failed = CURLE_FTP_ACCEPT_FAILED,
    ftp_weird_pass_reply = CURLE_FTP_WEIRD_PASS_REPLY,
    ftp_accept_timeout = CURLE_FTP_ACCEPT_TIMEOUT,
    ftp_weird_pasv_reply = CURLE_FTP_WEIRD_PASV_REPLY,
    ftp_weird_227_format = CURLE_FTP_WEIRD_227_FORMAT,
    ftp_cant_get_host = CURLE_FTP_CANT_GET_HOST,
    http2 = CURLE_HTTP2,
    ftp_couldnt_set_type = CURLE_FTP_COULDNT_SET_TYPE,
    partial_file = CURLE_PARTIAL_FILE,
    ftp_couldnt_retr_file = CURLE_FTP_COULDNT_RETR_FILE,
    obsolete20 = CURLE_OBSOLETE20,
    quote_error = CURLE_QUOTE_ERROR,
    http_returned_error = CURLE_HTTP_RETURNED_ERROR,
    write_error = CURLE_WRITE_ERROR,
    obsolete24 = CURLE_OBSOLETE24,
    upload_failed = CURLE_UPLOAD_FAILED,
    read_error = CURLE_READ_ERROR,
    out_of_memory = CURLE_OUT_OF_MEMORY,
    operation_timedout = CURLE_OPERATION_TIMEDOUT,
    obsolete29 = CURLE_OBSOLETE29,
    ftp_port_failed = CURLE_FTP_PORT_FAILED,
    ftp_couldnt_use_rest = CURLE_FTP_COULDNT_USE_REST,
    obsolete32 = CURLE_OBSOLETE32,
    range_error = CURLE_RANGE_ERROR,
    http_post_error = CURLE_HTTP_POST_ERROR,
    ssl_connect_error = CURLE_SSL_CONNECT_ERROR,
    bad_download_resume = CURLE_BAD_DOWNLOAD_RESUME,
    file_couldnt_read_file = CURLE_FILE_COULDNT_READ_FILE,
    ldap_cannot_bind = CURLE_LDAP_CANNOT_BIND,
    ldap_search_failed = CURLE_LDAP_SEARCH_FAILED,
    obsolete40 = CURLE_OBSOLETE40,
    function_not_found = CURLE_FUNCTION_NOT_FOUND,
    aborted_by_callback = CURLE_ABORTED_BY_CALLBACK,
    bad_function_argument = CURLE_BAD_FUNCTION_ARGUMENT,
    obsolete44 = CURLE_OBSOLETE44,
    interface_failed = CURLE_INTERFACE_FAILED,
    obsolete46 = CURLE_OBSOLETE46,
    too_many_redirects = CURLE_TOO_MANY_REDIRECTS,
    unknown_option = CURLE_UNKNOWN_OPTION,
    setopt_option_syntax = CURLE_SETOPT_OPTION_SYNTAX,
    obsolete50 = CURLE_OBSOLETE50,
    obsolete51 = CURLE_OBSOLETE51,
    got_nothing = CURLE_GOT_NOTHING,
    ssl_engine_notfound = CURLE_SSL_ENGINE_NOTFOUND,
    ssl_engine_setfailed = CURLE_SSL_ENGINE_SETFAILED,
    send_error = CURLE_SEND_ERROR,
    recv_error = CURLE_RECV_ERROR,
    obsolete57 = CURLE_OBSOLETE57,
    ssl_certproblem = CURLE_SSL_CERTPROBLEM,
    ssl_cipher = CURLE_SSL_CIPHER,
    peer_failed_verification = CURLE_PEER_FAILED_VERIFICATION,
    ssl_cacert = CURLE_SSL_CACERT,
    bad_content_encoding = CURLE_BAD_CONTENT_ENCODING,
    obsolete62 = CURLE_OBSOLETE62,
    filesize_exceeded = CURLE_FILESIZE_EXCEEDED,
    use_ssl_failed = CURLE_USE_SSL_FAILED,
    send_fail_rewind = CURLE_SEND_FAIL_REWIND,
    ssl_engine_initfailed = CURLE_SSL_ENGINE_INITFAILED,
    login_denied = CURLE_LOGIN_DENIED,
    tftp_notfound = CURLE_TFTP_NOTFOUND,
    tftp_perm = CURLE_TFTP_PERM,
    remote_disk_full = CURLE_REMOTE_DISK_FULL,
    tftp_illegal = CURLE_TFTP_ILLEGAL,
    tftp_unknownid = CURLE_TFTP_UNKNOWNID,
    remote_file_exists = CURLE_REMOTE_FILE_EXISTS,
    tftp_nosuchuser = CURLE_TFTP_NOSUCHUSER,
    obsolete75 = CURLE_OBSOLETE75,
    obsolete76 = CURLE_OBSOLETE76,
    ssl_cacert_badfile = CURLE_SSL_CACERT_BADFILE,
    remote_file_not_found = CURLE_REMOTE_FILE_NOT_FOUND,
    ssh = CURLE_SSH,
    ssl_shutdown_failed = CURLE_SSL_SHUTDOWN_FAILED,
    again = CURLE_AGAIN,
    ssl_crl_badfile = CURLE_SSL_CRL_BADFILE,
    ssl_issuer_error = CURLE_SSL_ISSUER_ERROR,
    ftp_pret_failed = CURLE_FTP_PRET_FAILED,
    rtsp_cseq_error = CURLE_RTSP_CSEQ_ERROR,
    rtsp_session_error = CURLE_RTSP_SESSION_ERROR,
    ftp_bad_file_list = CURLE_FTP_BAD_FILE_LIST,
    chunk_failed = CURLE_CHUNK_FAILED,
    no_connection_available = CURLE_NO_CONNECTION_AVAILABLE,
    ssl_pinnedpubkeynotmatch = CURLE_SSL_PINNEDPUBKEYNOTMATCH,
    ssl_invalidcertstatus = CURLE_SSL_INVALIDCERTSTATUS,
    http2_stream = CURLE_HTTP2_STREAM,
    recursive_api_call = CURLE_RECURSIVE_API_CALL,
    auth_error = CURLE_AUTH_ERROR,
    http3 = CURLE_HTTP3,
    quic_connect_error = CURLE_QUIC_CONNECT_ERROR,
    proxy = CURLE_PROXY,
    ssl_clientcert = CURLE_SSL_CLIENTCERT,
    unrecoverable_poll = CURLE_UNRECOVERABLE_POLL,
    curl_last = CURL_LAST
  };

  easy();
  easy(easy&) = delete;
  auto operator=(easy&) -> easy& = delete;
  easy(easy&&) = default;
  auto operator=(easy&&) -> easy& = default;
  ~easy();

  /// Sets an option to NULL / nullptr.
  auto unset(CURLoption option) -> code;

  /// Sets a numeric transfer option.
  auto set(CURLoption option, long parameter) -> code;

  /// Sets a string transfer option.
  /// @pre *parameter* must be a NULL-terminated string.
  auto set(CURLoption option, std::string_view parameter) -> code;

  /// Sets a write callback.
  auto set(write_callback fun) -> code;

  /// Sets a read callback.
  auto set(read_callback fun) -> code;

  /// Sets a MIME handle.
  auto set(mime handle) -> code;

  /// Sets a value of a HTTP header.
  /// @param name The header name, e.g., "User-Agent"
  /// @param value The header value, e.g., "Tenzir". If empty, the header will
  /// be deleted instead.
  auto set_http_header(std::string_view name, std::string_view value) -> code;

  /// Enumerates the list of all added headers.
  auto headers() -> generator<std::pair<std::string_view, std::string_view>>;

  /// `curl_easy_perform`
  auto perform() -> code;

  /// `curl_easy_reset`
  auto reset() -> void;

private:
  CURL* easy_{nullptr};
  std::unique_ptr<write_callback> on_write_{};
  std::unique_ptr<read_callback> on_read_{};
  std::unique_ptr<mime> mime_{};
  slist headers_;
};

/// @relates easy
auto to_string(easy::code code) -> std::string_view;

/// @relates easy
auto to_error(easy::code code) -> caf::error;

/// A group of transfers, corresponding to a cURL "multi" handle.
class multi {
public:
  /// The `CURLMcode` enum.
  enum class code {
    call_multi_perform = CURLM_CALL_MULTI_PERFORM,
    ok = CURLM_OK,
    bad_handle = CURLM_BAD_HANDLE,
    bad_easy_handle = CURLM_BAD_EASY_HANDLE,
    out_of_memory = CURLM_OUT_OF_MEMORY,
    internal_error = CURLM_INTERNAL_ERROR,
    bad_socket = CURLM_BAD_SOCKET,
    unknown_option = CURLM_UNKNOWN_OPTION,
    added_already = CURLM_ADDED_ALREADY,
    recursive_api_call = CURLM_RECURSIVE_API_CALL,
    wakeup_failure = CURLM_WAKEUP_FAILURE,
    bad_function_argument = CURLM_BAD_FUNCTION_ARGUMENT,
    aborted_by_callback = CURLM_ABORTED_BY_CALLBACK,
    unrecoverable_poll = CURLM_UNRECOVERABLE_POLL,
    last = CURLM_LAST
  };

  multi();

  /// Sets a multi option.
  auto set(CURLMoption option, auto parameter) -> code;

  /// Adds an easy handle.
  /// @returns `code::ok` iff the handle was added successfully.
  auto add(easy& handle) -> code;

  /// Removes a previously added easy handle.
  /// @returns `code::ok` iff the handle was removed successfully.
  auto remove(easy& handle) -> code;

  /// `curl_multi_poll`
  auto poll(std::chrono::milliseconds timeout) -> code;

  /// `curl_multi_perform`
  auto perform() -> std::pair<code, size_t>;

  /// Perform one round of transfers and waits afterwards up to a timeout
  /// to report the number of still running transfers.
  /// @returns The number of still running transfers.
  auto run(std::chrono::milliseconds timeout) -> caf::expected<size_t>;

  /// Loops and blocks until all outstanding transfers have completed.
  /// @param timeout The poll timeout for `curl_multi_poll`.
  /// @returns An error upon failure.
  auto loop(std::chrono::milliseconds timeout) -> caf::error;

  /// `curl_multi_info_read`
  auto info_read() -> generator<easy::code>;

private:
  struct curlm_deleter {
    auto operator()(CURLM* ptr) const noexcept -> void {
      // libcurl demands the following cleanup order:
      // (1) Remove easy handles
      // (2) Cleanup easy handles
      // (3) Clean up the multi handle
      // We cannot enforce (1) and (2) here because our easy handles don't have
      // shared ownership semantics. It's up to the user to add and remove them.
      if (ptr)
        curl_multi_cleanup(ptr);
    }
  };

  std::unique_ptr<CURLM, curlm_deleter> multi_;
};

/// @relates multi
auto to_string(multi::code code) -> std::string_view;

/// @relates easy
auto to_error(multi::code code) -> caf::error;

/// An interface for MIME handling based on the `curl_mime_*` functions.
class mime {
  friend class easy;

public:
  /// A MIME part with view semantics. Instances of this type are only valid
  /// while the corresponding MIME instance is valid.
  class part {
    friend class mime;

  public:
    /// Sets the name of the part.
    /// @param name The name of the part.
    /// @pre *name* must be a NULL-terminated string.
    auto name(std::string_view name) -> easy::code;

    /// Sets the content type of the part, e.g., `image/png`
    /// @param content_type The content type of the part.
    /// @pre *content_type* must be a NULL-terminated string.
    auto type(std::string_view content_type) -> easy::code;

    /// Sets the data of the MIME part by copying it from a buffer.
    /// @param buffer The data to copy into the part.
    auto data(std::span<const std::byte> buffer) -> easy::code;

    /// Sets the data by menas of a read callback.
    /// @param on_read The read callback to read in parts. The caller must
    /// ensure that the pointer remains valid.
    /// @pre `on_read != nullptr`
    auto data(read_callback* on_read) -> easy::code;

  private:
    explicit part(curl_mimepart* ptr);

    curl_mimepart* part_{nullptr};
  };

  /// Constructs a MIME handle.
  explicit mime(easy& handle);

  /// Adds a MIME part.
  auto add() -> part;

private:
  struct curl_mime_deleter {
    auto operator()(curl_mime* ptr) const noexcept -> void {
      if (ptr)
        curl_mime_free(ptr);
    }
  };

  std::unique_ptr<curl_mime, curl_mime_deleter> mime_;
};

/// An interface for URL handling based on the `curl_url_*` functions.
class url {
  friend class easy;

public:
  /// CURLUCode
  enum class code {
    ok = CURLUE_OK,
    bad_handle = CURLUE_BAD_HANDLE,
    bad_partpointer = CURLUE_BAD_PARTPOINTER,
    malformed_input = CURLUE_MALFORMED_INPUT,
    bad_port_number = CURLUE_BAD_PORT_NUMBER,
    unsupported_scheme = CURLUE_UNSUPPORTED_SCHEME,
    urldecode = CURLUE_URLDECODE,
    out_of_memory = CURLUE_OUT_OF_MEMORY,
    user_not_allowed = CURLUE_USER_NOT_ALLOWED,
    unknown_part = CURLUE_UNKNOWN_PART,
    no_scheme = CURLUE_NO_SCHEME,
    no_user = CURLUE_NO_USER,
    no_password = CURLUE_NO_PASSWORD,
    no_options = CURLUE_NO_OPTIONS,
    no_host = CURLUE_NO_HOST,
    no_port = CURLUE_NO_PORT,
    no_query = CURLUE_NO_QUERY,
    no_fragment = CURLUE_NO_FRAGMENT,
    no_zoneid = CURLUE_NO_ZONEID,
    bad_file_url = CURLUE_BAD_FILE_URL,
    bad_fragment = CURLUE_BAD_FRAGMENT,
    bad_hostname = CURLUE_BAD_HOSTNAME,
    bad_ipv6 = CURLUE_BAD_IPV6,
    bad_login = CURLUE_BAD_LOGIN,
    bad_password = CURLUE_BAD_PASSWORD,
    bad_path = CURLUE_BAD_PATH,
    bad_query = CURLUE_BAD_QUERY,
    bad_scheme = CURLUE_BAD_SCHEME,
    bad_slashes = CURLUE_BAD_SLASHES,
    bad_user = CURLUE_BAD_USER,
#if LIBCURL_VERSION_NUM >= 0x077500
    lacks_idn = CURLUE_LACKS_IDN,
#endif
    last = CURLUE_LAST,
  };

  /// CURLUPart
  enum class part {
    url = CURLUPART_URL,
    scheme = CURLUPART_SCHEME,
    user = CURLUPART_USER,
    password = CURLUPART_PASSWORD,
    options = CURLUPART_OPTIONS,
    host = CURLUPART_HOST,
    port = CURLUPART_PORT,
    path = CURLUPART_PATH,
    query = CURLUPART_QUERY,
    fragment = CURLUPART_FRAGMENT,
    zoneid = CURLUPART_ZONEID,
  };

  enum class flags : unsigned int {
    no_flags = 0,
    default_port = CURLU_DEFAULT_PORT,
    no_default_port = CURLU_NO_DEFAULT_PORT,
    default_scheme = CURLU_DEFAULT_SCHEME,
    non_support_scheme = CURLU_NON_SUPPORT_SCHEME,
    path_as_is = CURLU_PATH_AS_IS,
    disallow_user = CURLU_DISALLOW_USER,
    urldecode = CURLU_URLDECODE,
    urlencode = CURLU_URLENCODE,
    appendquery = CURLU_APPENDQUERY,
    guess_scheme = CURLU_GUESS_SCHEME,
    no_authority = CURLU_NO_AUTHORITY,
    allow_space = CURLU_ALLOW_SPACE,
#if LIBCURL_VERSION_NUM >= 0x077500
    punycode = CURLU_PUNYCODE,
#endif
#if LIBCURL_VERSION_NUM >= 0x080300
    puny2idn = CURLU_PUNY2IDN,
#endif
  };

  url();
  url(const url& other);
  auto operator=(const url& other) -> url&;
  url(url&& other) = default;
  auto operator=(url&& other) -> url& = default;

  auto set(part url_part, std::string_view str, flags = {}) -> code;

  auto get(part url_part, unsigned int flags = 0) const
    -> std::pair<code, std::optional<std::string>>;

private:
  struct curl_url_deleter {
    auto operator()(CURLU* ptr) const noexcept -> void {
      if (ptr)
        curl_url_cleanup(ptr);
    }
  };

  std::unique_ptr<CURLU, curl_url_deleter> url_;
};

/// @relates url
auto to_string(url::code code) -> std::string_view;

/// @relates url
auto to_string(const url& x) -> std::string;

/// @relates url
auto to_error(url::code code) -> caf::error;

/// URL-encodes a string.
/// @param str The input to encode.
/// @returns The encoded string.
auto escape(std::string_view str) -> std::string;

/// URL-encodes a record of parameters.
/// @param xs The key-value pairs to encode.
/// @returns The encoded string.
auto escape(const record& xs) -> std::string;

} // namespace tenzir::curl
