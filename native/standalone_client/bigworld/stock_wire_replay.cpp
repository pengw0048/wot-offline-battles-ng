// Offline replay files: tagged binary records, gzip-compressed on a native writer thread.
#include "stock_wire_replay.hpp"
#include "stock_wire_codec.hpp"
#include "stock_wire_replica_store.hpp"
#if defined(WOT_WIRE_HOST_BUILD)
#include <zlib.h>
#else
#include "zip/zlib.h"
#endif
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace stock_wire_replay {
namespace {
const char magic[8] = {'W', 'O', 'T', 'L', 'A', 'N', 'R', '2'};
const std::size_t record_header = 13;
const std::size_t max_record = 16u * 1024u * 1024u;
const int wire_kind = 1, frame_kind = 4, raw_kind = 5;
const char* writer_name = "offline_client_wire_native.ReplayWriter";
const char* reader_name = "offline_client_wire_native.ReplayReader";

}

class Writer {
public:
    explicit Writer(std::FILE* file) : file_(file) {
        std::memset(&zs_, 0, sizeof(zs_));
        if (deflateInit2(&zs_, 3, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK) { failed_ = true; error_ = "deflate init failed"; }
        else { initialized_ = true; queued_.push_back(std::string(magic, sizeof(magic))); }
        thread_ = std::thread([this] { run(); });
    }
    ~Writer() { finish(); }

    // Records from the main and receive threads share one clock; keep it monotonic.
    bool push(std::string record) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closing_ || failed_) return false;
        double t; std::memcpy(&t, &record[1], 8);
        t = std::max(t, last_t_); last_t_ = t;
        std::memcpy(&record[1], &t, 8);
        const int kind = static_cast<unsigned char>(record[0]);
        if (kind != 0 && kind != 3) ++records_;
        bytes_ += record.size();
        queued_.push_back(std::move(record));
        ready_.notify_one();
        return true;
    }
    // Flushes every queued record and the gzip trailer; safe to call twice.
    bool finish() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closing_ = true;
            ready_.notify_one();
        }
        if (thread_.joinable()) thread_.join();
        if (initialized_) { deflateEnd(&zs_); initialized_ = false; }
        if (file_) { if (std::fclose(file_) != 0 && !failed_) { failed_ = true; error_ = "close failed"; } file_ = nullptr; }
        return !failed_;
    }
    void abort() {
        { std::lock_guard<std::mutex> lock(mutex_); failed_ = true; if (error_.empty()) error_ = "aborted"; }
        finish();
    }
    bool failed() { std::lock_guard<std::mutex> lock(mutex_); return failed_; }
    std::string error() { std::lock_guard<std::mutex> lock(mutex_); return error_; }
    unsigned long long bytes() { std::lock_guard<std::mutex> lock(mutex_); return bytes_; }
    unsigned long long records() { std::lock_guard<std::mutex> lock(mutex_); return records_; }

private:
    void run() {
        for (;;) {
            std::string record;
            bool last = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return closing_ || failed_ || !queued_.empty(); });
                if (failed_) return;
                if (!queued_.empty()) { record.swap(queued_.front()); queued_.pop_front(); }
                last = closing_ && queued_.empty();
            }
            if (!record.empty() && !deflateChunk(record.data(), record.size(), Z_NO_FLUSH)) return;
            if (last) { deflateChunk(nullptr, 0, Z_FINISH); return; }
        }
    }
    bool deflateChunk(const char* data, std::size_t size, int flush) {
        if (!initialized_) return fail("deflate unavailable");
        zs_.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
        zs_.avail_in = static_cast<uInt>(size);
        unsigned char out[65536];
        int status;
        do {
            zs_.next_out = out;
            zs_.avail_out = sizeof(out);
            status = deflate(&zs_, flush);
            if (status == Z_STREAM_ERROR) return fail("deflate failed");
            std::size_t produced = sizeof(out) - zs_.avail_out;
            if (produced && std::fwrite(out, 1, produced, file_) != produced) return fail("write failed");
        } while (zs_.avail_out == 0 || (flush == Z_FINISH && status != Z_STREAM_END));
        return true;
    }
    bool fail(const char* why) {
        std::lock_guard<std::mutex> lock(mutex_);
        failed_ = true;
        error_ = why;
        return false;
    }

    std::FILE* file_;
    z_stream zs_;
    bool initialized_ = false;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::string> queued_;
    bool closing_ = false, failed_ = false;
    std::string error_;
    unsigned long long bytes_ = 0, records_ = 0;
    double last_t_ = 0.;
    std::thread thread_;
};

namespace {
std::string recordHeader(int kind, double t, std::size_t size) {
    std::string record(record_header, '\0');
    record[0] = static_cast<char>(kind);
    uint32_t length = static_cast<uint32_t>(size);
    std::memcpy(&record[1], &t, 8);
    std::memcpy(&record[9], &length, 4);
    return record;
}

class Reader {
public:
    explicit Reader(std::FILE* file) : file_(file) {
        std::memset(&zs_, 0, sizeof(zs_));
        ok_ = inflateInit2(&zs_, 31) == Z_OK;
    }
    ~Reader() { if (ok_) inflateEnd(&zs_); if (file_) std::fclose(file_); }
    // Exactly n inflated bytes, false at a clean end of stream before the first byte.
    bool read(std::string& out, std::size_t n, bool& clean_end) {
        clean_end = false;
        out.clear();
        while (out.size() < n) {
            if (pending_.size() > consumed_) {
                std::size_t take = std::min(n - out.size(), pending_.size() - consumed_);
                out.append(pending_, consumed_, take);
                consumed_ += take;
                continue;
            }
            pending_.clear(); consumed_ = 0;
            if (ended_) { if (out.empty()) clean_end = true; return false; }
            if (!inflateMore()) return false;
        }
        return true;
    }
    bool trailingData() {
        if (pending_.size() > consumed_) return true;
        if (!ended_) { pending_.clear(); consumed_ = 0; if (inflateMore() && !pending_.empty()) return true; }
        unsigned char extra;
        return std::fread(&extra, 1, 1, file_) == 1;
    }
    std::string error;

private:
    bool inflateMore() {
        if (!ok_) { error = "inflate init failed"; return false; }
        unsigned char out[65536];
        for (;;) {
            if (zs_.avail_in == 0) {
                std::size_t got = std::fread(input_, 1, sizeof(input_), file_);
                if (!got) { error = "truncated replay"; return false; }
                zs_.next_in = input_; zs_.avail_in = static_cast<uInt>(got);
            }
            zs_.next_out = out; zs_.avail_out = sizeof(out);
            int status = inflate(&zs_, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END) { error = "corrupt replay stream"; return false; }
            pending_.append(reinterpret_cast<char*>(out), sizeof(out) - zs_.avail_out);
            if (status == Z_STREAM_END) { ended_ = true; return true; }
            if (!pending_.empty()) return true;
        }
    }
    std::FILE* file_;
    z_stream zs_;
    bool ok_ = false, ended_ = false;
    unsigned char input_[65536];
    std::string pending_;
    std::size_t consumed_ = 0;
public:
    // Replica deltas recorded from the wire merge here exactly as on the live receive thread.
    stock_wire::ReplicaStore replicas;
};

std::FILE* openFile(PyObject* path, const char* mode) {
#if defined(_WIN32)
    if (PyUnicode_Check(path)) {
        std::wstring wide(PyUnicode_GET_SIZE(path), L'\0');
        if (PyUnicode_AsWideChar(reinterpret_cast<PyUnicodeObject*>(path), &wide[0], wide.size()) < 0) return nullptr;
        std::wstring wmode(mode, mode + std::strlen(mode));
        return _wfopen(wide.c_str(), wmode.c_str());
    }
#endif
    const char* narrow = PyString_Check(path) ? PyString_AS_STRING(path) : nullptr;
    if (!narrow) { PyErr_SetString(PyExc_TypeError, "replay path must be text"); return nullptr; }
    return std::fopen(narrow, mode);
}

void destroyWriter(PyObject* capsule) { delete static_cast<std::shared_ptr<Writer>*>(PyCapsule_GetPointer(capsule, writer_name)); }
Writer* writerPointer(PyObject* capsule) {
    auto* held = static_cast<std::shared_ptr<Writer>*>(PyCapsule_GetPointer(capsule, writer_name));
    return held ? held->get() : nullptr;
}
void destroyReader(PyObject* capsule) { delete static_cast<Reader*>(PyCapsule_GetPointer(capsule, reader_name)); }

PyObject* openWriter(PyObject*, PyObject* args) {
    PyObject* path;
    if (!PyArg_ParseTuple(args, "O", &path)) return nullptr;
    std::FILE* file = openFile(path, "wb");
    if (!file) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_IOError, "cannot create replay file"); return nullptr; }
    return PyCapsule_New(new std::shared_ptr<Writer>(std::make_shared<Writer>(file)), writer_name, destroyWriter);
}

PyObject* writeRecord(PyObject*, PyObject* args) {
    PyObject* capsule; int kind; double t; PyObject* value;
    if (!PyArg_ParseTuple(args, "OidO", &capsule, &kind, &t, &value)) return nullptr;
    auto* writer = writerPointer(capsule);
    if (!writer) return nullptr;
    if (kind < 0 || kind > 255) { PyErr_SetString(PyExc_ValueError, "bad replay record kind"); return nullptr; }
    std::string payload;
    // Gameplay messages use the compact schema frame when they fit it.
    if (kind == wire_kind && stock_wire::encode(value, payload)) kind = frame_kind;
    else {
        PyErr_Clear();
        if (!stock_wire::encode_any(value, payload)) return nullptr;
    }
    if (payload.size() > max_record) { PyErr_SetString(PyExc_ValueError, "replay record exceeds size bound"); return nullptr; }
    std::string record = recordHeader(kind, t, payload.size());
    record += payload;
    if (!writer->push(std::move(record))) {
        PyErr_SetString(PyExc_IOError, ("replay writer stopped: " + writer->error()).c_str());
        return nullptr;
    }
    return PyLong_FromUnsignedLongLong(writer->bytes());
}

PyObject* finishWriter(PyObject*, PyObject* args) {
    PyObject* capsule; int aborted = 0;
    if (!PyArg_ParseTuple(args, "O|i", &capsule, &aborted)) return nullptr;
    auto* writer = writerPointer(capsule);
    if (!writer) return nullptr;
    bool ok;
    Py_BEGIN_ALLOW_THREADS
    if (aborted) { writer->abort(); ok = false; } else ok = writer->finish();
    Py_END_ALLOW_THREADS
    std::string error = writer->error();
    return Py_BuildValue("(Os)", ok ? Py_True : Py_False, error.c_str());
}

PyObject* openReader(PyObject*, PyObject* args) {
    PyObject* path;
    if (!PyArg_ParseTuple(args, "O", &path)) return nullptr;
    std::FILE* file = openFile(path, "rb");
    if (!file) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_IOError, "cannot open replay file"); return nullptr; }
    auto* reader = new Reader(file);
    std::string head; bool end;
    if (!reader->read(head, sizeof(magic), end) || std::memcmp(head.data(), magic, sizeof(magic)) != 0) {
        delete reader;
        PyErr_SetString(PyExc_ValueError, "not an offline replay of this version");
        return nullptr;
    }
    return PyCapsule_New(reader, reader_name, destroyReader);
}

// (kind, t, value), or None at the clean end of the stream.
PyObject* readRecord(PyObject*, PyObject* args) {
    PyObject* capsule;
    if (!PyArg_ParseTuple(args, "O", &capsule)) return nullptr;
    auto* reader = static_cast<Reader*>(PyCapsule_GetPointer(capsule, reader_name));
    if (!reader) return nullptr;
    std::string head, payload; bool end;
    if (!reader->read(head, record_header, end)) {
        if (end) Py_RETURN_NONE;
        PyErr_SetString(PyExc_ValueError, reader->error.empty() ? "truncated replay record" : reader->error.c_str());
        return nullptr;
    }
    double t; uint32_t length;
    std::memcpy(&t, &head[1], 8);
    std::memcpy(&length, &head[9], 4);
    if (length > max_record || !reader->read(payload, length, end)) {
        PyErr_SetString(PyExc_ValueError, "truncated or oversized replay record");
        return nullptr;
    }
    int kind = static_cast<unsigned char>(head[0]);
    const auto* bytes = reinterpret_cast<const unsigned char*>(payload.data());
    if (kind == raw_kind) {
        // A received frame; a replica delta before its full snapshot reads as None.
        if (stock_wire::frame_message(bytes, payload.size()) != stock_wire::message_id("replica_snapshot")) {
            PyObject* message = stock_wire::decode(bytes, payload.size());
            return message ? Py_BuildValue("(idN)", wire_kind, t, message) : nullptr;
        }
        stock_native_client_state::Value message; std::string error;
        if (!stock_wire::decode_value(bytes, payload.size(), message, error)) {
            PyErr_SetString(PyExc_ValueError, error.c_str());
            return nullptr;
        }
        try {
            PyObject* capsule = stock_wire::snapshot_capsule(reader->replicas.apply(std::move(message)));
            PyObject* view = capsule ? stock_wire::replica_view(capsule) : nullptr;
            Py_XDECREF(capsule);
            return view ? Py_BuildValue("(idN)", wire_kind, t, view) : nullptr;
        } catch (const std::exception&) {
            return Py_BuildValue("(idO)", raw_kind, t, Py_None);
        }
    }
    PyObject* value = kind == frame_kind ? stock_wire::decode(bytes, payload.size())
                                         : stock_wire::decode_any(bytes, payload.size());
    if (!value) return nullptr;
    return Py_BuildValue("(idN)", kind == frame_kind ? wire_kind : kind, t, value);
}

PyObject* writerRecords(PyObject*, PyObject* args) {
    PyObject* capsule;
    if (!PyArg_ParseTuple(args, "O", &capsule)) return nullptr;
    auto* writer = writerPointer(capsule);
    if (!writer) return nullptr;
    return PyLong_FromUnsignedLongLong(writer->records());
}

PyObject* readerTrailing(PyObject*, PyObject* args) {
    PyObject* capsule;
    if (!PyArg_ParseTuple(args, "O", &capsule)) return nullptr;
    auto* reader = static_cast<Reader*>(PyCapsule_GetPointer(capsule, reader_name));
    if (!reader) return nullptr;
    if (reader->trailingData()) Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}
}

std::shared_ptr<Writer> writer(PyObject* capsule) {
    auto* held = PyCapsule_IsValid(capsule, writer_name)
        ? static_cast<std::shared_ptr<Writer>*>(PyCapsule_GetPointer(capsule, writer_name)) : nullptr;
    return held ? *held : std::shared_ptr<Writer>();
}

bool record_frame(Writer& target, const std::string& frame, double t) {
    std::string record = recordHeader(raw_kind, t, frame.size());
    record += frame;
    return target.push(std::move(record));
}

PyMethodDef* methods() {
    static PyMethodDef table[] = {
        {"replay_writer", openWriter, METH_VARARGS, "Open a replay file for the native writer thread."},
        {"replay_write", writeRecord, METH_VARARGS, "Queue one encoded replay record."},
        {"replay_finish", finishWriter, METH_VARARGS, "Flush and close a replay writer, or abort it."},
        {"replay_reader", openReader, METH_VARARGS, "Open a replay file for reading."},
        {"replay_read", readRecord, METH_VARARGS, "Read the next replay record."},
        {"replay_trailing", readerTrailing, METH_VARARGS, "Whether bytes follow the read records."},
        {"replay_count", writerRecords, METH_VARARGS, "Records queued so far, from every thread."},
        {nullptr, nullptr, 0, nullptr}};
    return table;
}

}
