#pragma once

#include <util/generic/flags.h>
#include <util/generic/ptr.h>
#include <util/generic/vector.h>
#include <util/stream/mem.h>
#include <util/stream/output.h>
#include <util/system/atomic.h>

#include "location.h"
#include "neh.h"
#include "rpc.h"

//common primitives for http/http2

namespace NNeh {
    class IHttpRequest: public IRequest {
    public:
        using IRequest::SendReply;
        virtual void SendReply(TData& data, const TString& headers) = 0;
        virtual const THttpHeaders& Headers() const = 0;
    };

    namespace NHttp {
        struct TFdLimits {
        public:
            TFdLimits()
                : Soft(10000)
                , Hard(15000)
            {
            }

            inline size_t Delta() const noexcept {
                return ExceedLimit(Hard, Soft);
            }

            inline static size_t ExceedLimit(size_t val, size_t limit) noexcept {
                return val > limit ? val - limit : 0;
            }

            volatile size_t Soft;
            volatile size_t Hard;
        };

        template <class T>
        class TLockFreeSequence {
        public:
            inline TLockFreeSequence() {
                memset((void*)T_, 0, sizeof(T_));
            }

            inline ~TLockFreeSequence() {
                for (size_t i = 0; i < Y_ARRAY_SIZE(T_); ++i) {
                    delete[] T_[i];
                }
            }

            inline T& Get(size_t n) {
                const size_t i = GetValueBitCount(n + 1) - 1;

                return GetList(i)[n + 1 - (((size_t)1) << i)];
            }

        private:
            inline T* GetList(size_t n) {
                T* volatile* t = T_ + n;

                while (!AtomicGet(*t)) {
                    TArrayHolder<T> nt(new T[((size_t)1) << n]);

                    if (AtomicCas(t, nt.Get(), nullptr)) {
                        return nt.Release();
                    }
                }

                return *t;
            }

        private:
            T* volatile T_[sizeof(size_t) * 8];
        };

        class TRequestData: public TNonCopyable {
        public:
            typedef TAutoPtr<TRequestData> TPtr;
            typedef TVector<IOutputStream::TPart> TParts;

            inline TRequestData(size_t memSize)
                : Mem(memSize)
            {
            }

            inline void SendTo(IOutputStream& io) const {
                io.Write(~Parts_, +Parts_);
            }

            inline void AddPart(const void* buf, size_t len) noexcept {
                Parts_.push_back(IOutputStream::TPart(buf, len));
            }

            const TParts& Parts() const noexcept {
                return Parts_;
            }

            TVector<char> Mem;

        private:
            TParts Parts_;
        };

        struct TRequestGet {
            static TRequestData::TPtr Build(const TMessage& msg, const TParsedLocation& loc) {
                TRequestData::TPtr req(new TRequestData(50 + +loc.Service + +msg.Data + +loc.Host));
                TMemoryOutput out(~req->Mem, +req->Mem);

                out << AsStringBuf("GET /") << loc.Service;

                if (!!msg.Data) {
                    out << '?' << msg.Data;
                }

                out << AsStringBuf(" HTTP/1.1\r\nHost: ") << loc.Host;

                if (!!loc.Port) {
                    out << AsStringBuf(":") << loc.Port;
                }

                out << AsStringBuf("\r\n\r\n");

                req->AddPart(~req->Mem, out.Buf() - ~req->Mem);
                return req;
            }

            static inline TStringBuf Name() noexcept {
                return AsStringBuf("http");
            }
        };

        struct TRequestPost {
            static TRequestData::TPtr Build(const TMessage& msg, const TParsedLocation& loc) {
                TRequestData::TPtr req(new TRequestData(100 + +loc.Service + +loc.Host));
                TMemoryOutput out(~req->Mem, +req->Mem);

                out << AsStringBuf("POST /") << loc.Service
                    << AsStringBuf(" HTTP/1.1\r\nHost: ") << loc.Host;

                if (!!loc.Port) {
                    out << AsStringBuf(":") << loc.Port;
                }

                out << AsStringBuf("\r\nContent-Length: ") << +msg.Data << AsStringBuf("\r\n\r\n");

                req->AddPart(~req->Mem, out.Buf() - ~req->Mem);
                req->AddPart(~msg.Data, +msg.Data);
                return req;
            }

            static inline TStringBuf Name() noexcept {
                return AsStringBuf("post");
            }
        };

        struct TRequestFull {
            static TRequestData::TPtr Build(const TMessage& msg, const TParsedLocation&) {
                TRequestData::TPtr req(new TRequestData(0));
                req->AddPart(~msg.Data, +msg.Data);
                return req;
            }

            static inline TStringBuf Name() noexcept {
                return AsStringBuf("full");
            }
        };

        enum class ERequestType {
            Any = 0,
            Post,
            Get,
            Put,
            Delete
        };

        enum class ERequestFlag {
            None = 0,
            /** use absoulte uri for proxy requests in the first request line
         * POST http://ya.ru HTTP/1.1
         * @see https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html#sec5.1.2
         */
            AbsoluteUri = 1,
        };

        Y_DECLARE_FLAGS(ERequestFlags, ERequestFlag)
        Y_DECLARE_OPERATORS_FOR_FLAGS(ERequestFlags)

        static constexpr ERequestType DefaultRequestType = ERequestType::Any;

        extern const TString DefaultContentType;

        /*
        @brief  MakeFullRequest transmutes http/post/http2/post2 message to full/full2 with additional HTTP headers
                                and/or content data.

        @note   If reqType is Any, then request type is POST, unless content is empty and schema prefix is http/https/http2,
                in that case request type is GET.
     */
        bool MakeFullRequest(TMessage& msg //will get url from msg.Data
                             ,
                             const TStringBuf headers, const TStringBuf content, const TStringBuf contentType = DefaultContentType, ERequestType reqType = DefaultRequestType, ERequestFlags flags = ERequestFlag::None);

        bool MakeFullRequest(TMessage& msg, const TVector<TString>& urlParts //will construct url from urlParts, msg.Data is not used
                             ,
                             const TStringBuf headers, const TStringBuf content, const TStringBuf contentType = DefaultContentType, ERequestType reqType = DefaultRequestType, ERequestFlags flags = ERequestFlag::None);

        size_t GetUrlPartsLength(const TVector<TString>& urlParts);
        //part1&part2&...
        void JoinUrlParts(const TVector<TString>& urlParts, IOutputStream& out);
        //'?' + JoinUrlParts
        void WriteUrlParts(const TVector<TString>& urlParts, IOutputStream& out);

    }
}
