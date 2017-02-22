/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#include <array>
#include <queue>
#include "base_coders.hh"
#include "lepton_codec.hh"
#include "../../io/MuxReader.hh"
#include "aligned_block.hh"
#include "bool_decoder.hh"
#include "vp8_encoder.hh"


struct ResizableByteBufferListNode : public Sirikata::MuxReader::ResizableByteBuffer {
    uint8_t stream_id;
    ResizableByteBufferListNode *next;
    ResizableByteBufferListNode(){
        stream_id = 0;
        next = NULL;
    }
};
struct ResizableByteBufferList {
    ResizableByteBufferListNode * head;
    ResizableByteBufferListNode * tail;
    ResizableByteBufferList() {
        head = NULL;
        tail = NULL;
    }
    void push(ResizableByteBufferListNode * item) {
        always_assert(item);
        if (!tail) {
            always_assert(!head);
            head = tail = item;
        } else {
            always_assert(!tail->next);
            tail->next = item;
            tail = item;
        }
    }
    bool empty()const {
        return head == NULL && tail == NULL;
    }
    bool size_gt_1()const {
        return head != tail;
    }
    ResizableByteBufferListNode * front() {
        return head;
    }
    const ResizableByteBufferListNode * front() const{
        return head;
    }
    ResizableByteBufferListNode * pop() {
        always_assert(!empty());
        auto retval = head;
        if (tail == head) {
            always_assert(tail->next == NULL);
            head = NULL;
            tail = NULL;
            return retval;
        }
        head = head->next;
        return retval;
    }
};

class VP8ComponentDecoder : public BaseDecoder, public VP8ComponentEncoder {
public:
    class SendToVirtualThread {
        ResizableByteBufferList vbuffers[Sirikata::MuxReader::MAX_STREAM_ID];
        uint8_t thread_target[Sirikata::MuxReader::MAX_STREAM_ID]; // 0 is the current thread
        GenericWorker *all_workers;
        bool eof;
        bool first;
        void set_eof();
    public:
        SendToVirtualThread();
        void init(GenericWorker *generic_workers);
        void send(ResizableByteBufferListNode *data);
        ResizableByteBufferListNode* read(Sirikata::MuxReader&reader, uint8_t stream_id);
        void read_all(Sirikata::MuxReader&reader);
    };
    class SendToActualThread {
    public:
        ResizableByteBufferList vbuffers[Sirikata::MuxReader::MAX_STREAM_ID];
    };
    
private:
    SendToActualThread send_to_actual_thread_state;
    Sirikata::DecoderReader *str_in {};
    //const std::vector<uint8_t, Sirikata::JpegAllocator<uint8_t> > *file_;
    Sirikata::MuxReader mux_reader_;
    SendToVirtualThread mux_splicer;
    std::vector<ThreadHandoff> thread_handoff_;
    Sirikata::Array1d<std::pair <Sirikata::MuxReader::ResizableByteBuffer::const_iterator,
                                 Sirikata::MuxReader::ResizableByteBuffer::const_iterator>,
                      Sirikata::MuxReader::MAX_STREAM_ID> streams_;

    VP8ComponentDecoder(const VP8ComponentDecoder&) = delete;
    VP8ComponentDecoder& operator=(const VP8ComponentDecoder&) = delete;
    static void worker_thread(ThreadState *, int thread_id, UncompressedComponents * const colldata);
    template <bool force_memory_optimized>
    void initialize_thread_id(int thread_id, int target_thread_state,
                              BlockBasedImagePerChannel<force_memory_optimized>& framebuffer);

    int virtual_thread_id_;
public:
    VP8ComponentDecoder(bool do_threading);
    // reads the threading information and uses mux_reader_ to create the streams_ 
    // returns the bound of each threads' max_luma (non inclusive) responsibility in the file
    template <bool force_memory_optimized>
    std::vector<ThreadHandoff> initialize_decoder_state(
        const UncompressedComponents * const colldata,
        // quantization_tables
        Sirikata::Array1d<BlockBasedImagePerChannel<force_memory_optimized>,
                          MAX_NUM_THREADS>& framebuffer); // framebuffer
    virtual std::vector<ThreadHandoff> initialize_baseline_decoder(const UncompressedComponents * const colldata,
                                             Sirikata::Array1d<BlockBasedImagePerChannel<true>,
                                                               MAX_NUM_THREADS>& framebuffer);
    void registerWorkers(GenericWorker *workers, unsigned int num_workers) {
        this->VP8ComponentEncoder::registerWorkers(workers, num_workers);
    }
    GenericWorker *getWorker(unsigned int i) {
        always_assert(i < num_registered_workers_);
        return &spin_workers_[i];
    }
    size_t get_model_memory_usage() const {
        return model_memory_used();
    }
    size_t get_model_worker_memory_usage() const {
        return model_worker_memory_used();
    }
    ~VP8ComponentDecoder();
    void initialize(Sirikata::DecoderReader *input,
                    const std::vector<ThreadHandoff>& thread_transition_info);
    //necessary to implement the BaseDecoder interface. Thin wrapper around vp8_decoder
    virtual CodingReturnValue decode_chunk(UncompressedComponents*dst);
    virtual void decode_row(int target_thread_id,
                            BlockBasedImagePerChannel<true>& image_data, // FIXME: set image_data to true
                            Sirikata::Array1d<uint32_t,
                                              (uint32_t)ColorChannel::
                                              NumBlockTypes> component_size_in_blocks,
                            int component,
                            int curr_y);
    virtual void clear_thread_state(int thread_id, int target_thread_state, BlockBasedImagePerChannel<true>& framebuffer);
};
