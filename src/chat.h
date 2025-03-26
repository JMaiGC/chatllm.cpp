#pragma once

#include <ggml.h>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <map>
#include <set>
#include <vector>
#include <random>
#include <chrono>
#include "basics.h"
#include "tokenizer.h"
#include "vectorstore.h"
#include "backend.h"

namespace chatllm
{
    enum ChatFormat
    {
        CHAT = 0,
        COMPLETION,
        QA,
    };

    enum ModelPurpose
    {
        Chat = 0,
        TextEmbedding,
        Ranker,
    };

    enum ChatModelAccessPoint
    {
        Text            = 0x01,
        ImageInput      = 0x02,
        ImageOutput     = 0x04,
        AudioInput      = 0x08,
        AudioOutput     = 0x10,
    };

    typedef uint16_t ChatModelAccessPoints;

    enum MsgRole
    {
        Auto = 0,
        // System = 1, // Not used.
        User = 2,
        Assistant = 3,
        Tool = 4,

        LAST,
    };

    class Message
    {
    public:
        Message(const std::string &s, MsgRole role, int round)
          : content(s), role(role), round(round)
        {
        }

        Message(const Message &m)
          : content(m.content), role(m.role), round(m.round)
        {
        }

        Message &operator =(const chatllm::Message &m);

    public:
        std::string content;
        const MsgRole role;
        const int round;
    };

    class Messages
    {
    public:
        Messages();

        void push_back(const std::string &content, MsgRole role);
        void push_back(const Message &m);
        void clear(void);

        size_t size() const
        {
            return history.size();
        }

        Message & operator [](size_t index)
        {
            return history[index];
        }

        const Message & operator [](size_t index) const
        {
            return history[index];
        }

        Message & back(void)
        {
            return history.back();
        }

        const Message & back(void) const
        {
            return history.back();
        }

        void move_cursor_to_end(void) const;
        void move_cursor_to_end(void);

    public:
        std::vector<Message> history;
        int cursor;
    protected:
        std::string sep;
        bool auto_aggregate;
        int round;
    };

    std::string to_string(ggml::tensor *tensor, bool with_data = true);

    struct BaseConfig
    {
        // common attributes
        ggml::type dtype;
        int vocab_size;
        int hidden_size;
        int num_attention_heads;
        int num_hidden_layers;
        int intermediate_size;
        // for sequence generation
        int max_length;
        // for tokenizer
        int bos_token_id;
        int eos_token_id;
        int pad_token_id;
        int sep_token_id;
    };

    std::string trim(std::string str, const char *spaces = " \t");

    class BaseHistoryEncoder;

    class BaseTokenizer
    {
    public:
        BaseTokenizer(const BaseConfig &config,
                        BaseHistoryEncoder *chat_encoder,
                        BaseHistoryEncoder *qa_encoder = nullptr,
                        BaseHistoryEncoder *completion_encoder = nullptr);

        virtual ~BaseTokenizer() = default;

        virtual size_t load(tokenizer::DataReader *buffer, int n_vocab) = 0;

        virtual void encode(const std::string &text, std::vector<int> &ids) const;
        std::vector<int> encode(const std::string &text) const;

        virtual void encode_external_text_completion(const std::string &text, std::vector<int> &ids) const;

        virtual void encode_qa(const std::string &q, const std::string &a, std::vector<int> &ids) const;

        virtual std::string decode(const std::vector<int> &ids) const;

        virtual std::vector<int> encode_history(const Messages &history, int max_length,
                                                const bool incremental = false,
                                                const bool ai_opening = true,
                                                const bool reversed_role = false);
        virtual std::vector<int> encode_history(BaseHistoryEncoder *encoder, const Messages &history, int max_length,
                                                const bool incremental = false,
                                                const bool ai_opening = true,
                                                const bool reversed_role = false);
        virtual std::vector<int> encode_sys_prompt(void);

        void set_system_prompt(const std::string &prompt) { sys_prompt = prompt; }
        const std::string &get_system_prompt(void) { return sys_prompt; }
        virtual void set_additional_args(const std::map<std::string, std::string> &args) {}

        virtual bool is_terminate_token_id(int id) const;
        virtual bool is_special_id(int id) const { return false; }
        int get_vocab_size(void) const { return vocab_size; }

        void set_chat_format(ChatFormat format) { this->format = format; }
        ChatFormat get_chat_format(void) const { return format; }

        virtual void set_skip_sys_prompt(bool skip);

        int bos_token_id;
        int eos_token_id;
        int pad_token_id;
        int sep_token_id;

    protected:
        virtual int get_history_start(const Messages &history, int max_length) const;

        virtual std::string preprocess(const std::string &text) const;
        virtual std::string postprocess(const std::string &text) const;

    public:
        tokenizer::Processor *tp;
    protected:
        std::string sys_prompt;
        const int max_length;
        ChatFormat format;
        BaseHistoryEncoder *chat_encoder;
        BaseHistoryEncoder *completion_encoder;
        BaseHistoryEncoder *qa_encoder;
        bool auto_add_bos;
        std::set<int> terminate_ids;
    private:
        const int vocab_size;
    };

    class BaseHistoryEncoder
    {
    public:
        BaseHistoryEncoder() : skip_sys_prompt(false), tokenizer(nullptr) {}

        virtual void append_sys_prompt(std::vector<int> &ids) const;
        virtual void append_user(int round_idx, const std::string &user, std::vector<int> &ids) const;
        virtual void append_ai(int round_idx, const std::string &ai, std::vector<int> &ids) const = 0;
        virtual void append_tool(int round_idx, const std::string &tool, std::vector<int> &ids) const;

        virtual void append_ai_opening(int round_idx, std::vector<int> &ids) const;
        virtual void append_user_opening(int round_idx, std::vector<int> &ids) const;

        virtual void append_message(const Message &msg, std::vector<int> &ids) const;

        void set_tokenizer(BaseTokenizer *tokenizer)
        {
            this->tokenizer = tokenizer;
        }
    public:
        bool skip_sys_prompt;
    protected:
        BaseTokenizer *tokenizer;
    };

    class GGMLContext
    {
    public:
        GGMLContext() : gctx_(nullptr) {}

        GGMLContext(ggml_init_params params) : gctx_(ggml_init(params))
        {
            CHATLLM_CHECK(gctx_) << "failed to init ggml context";
        }
        // copy constructor
        GGMLContext(const GGMLContext &) = delete;
        // move constructor
        GGMLContext(GGMLContext &&other) : gctx_(other.gctx_) { other.gctx_ = nullptr; }

        ~GGMLContext() { reset(); }

        // copy assignment
        GGMLContext &operator=(const GGMLContext &) = delete;
        // move assignment
        GGMLContext &operator=(GGMLContext &&other)
        {
            reset();
            gctx_ = other.gctx_;
            other.gctx_ = nullptr;
            return *this;
        }

        ggml_context *get() const { return gctx_; }

        void reset()
        {
            if (gctx_)
            {
                ggml_free(gctx_);
                gctx_ = nullptr;
            }
        }

    private:
        ggml_context *gctx_;
    };

    class InitContext : public ComputeContext
    {
    public:
        InitContext(BackendContext *backend_context = nullptr) : ComputeContext(backend_context)
        {
            cache_dtype = ggml::type::GGML_TYPE_F16;
        }
        struct ggml_context *get_ctx() override { return gctx.get(); }
    public:
        GGMLContext gctx;
        ggml::type dtype;
        ggml::type cache_dtype;
    };

    class CacheTypeChanger
    {
    public:
        CacheTypeChanger(InitContext *ctx, ggml::type cache_dtype): ctx(ctx)
        {
            _type = ctx->cache_dtype;
            ctx->cache_dtype = cache_dtype;
        }

        ~CacheTypeChanger()
        {
            ctx->cache_dtype = _type;
        }

        operator InitContext *() const
        {
            return ctx;
        }
    private:
        InitContext *ctx;
        ggml::type   _type;
    };

    class LayerMover
    {
    public:
        LayerMover(InitContext *ctx, int layer_id): ctx(ctx)
        {
            ctx->move_to_layer(layer_id);
        }

        operator InitContext *() const
        {
            return ctx;
        }
    private:
        InitContext *ctx;
    };

    class CPUMover
    {
    public:
        CPUMover(ComputeContext *ctx, bool activated): ctx(ctx), activated(activated)
        {
            if (activated)
                ctx->backend_cpu_override(true);
        }

        ~CPUMover()
        {
            if (activated)
                ctx->backend_cpu_override(false);
        }

        operator InitContext *() const
        {
            return dynamic_cast<InitContext *>(ctx);
        }

        operator ComputeContext *() const
        {
            return ctx;
        }
    private:
        ComputeContext *ctx;
        const bool activated;
    };

    class ChunkInterceptor;

    class BaseStreamer
    {
    public:
        enum TextType
        {
            META            = 1,    // print a whole line: general information
            ERR             = 2,    // print a whole line: error message
            REF             = 3,    // print a whole line: reference
            REWRITTEN_QUERY = 4,    // print a whole line: rewritten query
            HISTORY_USER    = 5,    // print a whole line: user input history
            HISTORY_AI      = 6,    // print a whole line: AI output history
            TOOL_CALLING    = 7,
            EMBEDDING       = 8,
            RANKING         = 9,
            TOKEN_IDS       =10,
            LOGGING         =11,
            BEAM_SEARCH     =12,
        };
        BaseStreamer(BaseTokenizer *tokenizer);
        virtual ~BaseStreamer() = default;
        virtual void put(const std::vector<int> &output_ids);
        virtual void put_chunk(bool first, const std::string &chunk) = 0;
        virtual void putln(const std::string &line, TextType type = TextType::META) = 0;
        virtual void end();

        virtual void set_interceptor(ChunkInterceptor *interceptor);
        virtual void remove_interceptor(void);

        virtual void set_tokenizer(BaseTokenizer *tokenizer)
        {
            this->tokenizer = tokenizer;
        }

        // used for RAG
        void put_reference(const std::string &line)
        {
            putln(line, TextType::REF);
        }
        void put_rewritten_query(const std::string &line)
        {
            putln(line, TextType::REWRITTEN_QUERY);
        }
        void put_history_user(const std::string &line)
        {
            putln(line, TextType::HISTORY_USER);
        }
        void put_history_ai(const std::string &line)
        {
            putln(line, TextType::HISTORY_AI);
        }
        void put_tool_calling(const std::string &line)
        {
            putln(line, TextType::TOOL_CALLING);
        }
    public:
        bool is_prompt;
        BaseTokenizer *tokenizer;
        int log_level;
    protected:
        bool is_first;
        size_t print_len;
        std::vector<int> token_cache;
        ChunkInterceptor *interceptor;

        virtual void call_put_chunk(bool first, const std::string &chunk);
    };

    class ChunkInterceptor
    {
    public:
        ChunkInterceptor() : streamer(nullptr) {}
        virtual ~ChunkInterceptor() {}
        virtual void intercept(BaseStreamer *streamer) { this->streamer = streamer; }
        virtual void put_chunk(bool first, const std::string &chunk) { if (streamer) streamer->put_chunk(first, chunk); }
        virtual void end() { }
    protected:
        BaseStreamer *streamer;
    };

    class MappedFile : public tokenizer::DataReader
    {
    public:
        MappedFile(const std::string &path);
        ~MappedFile();

        int64_t tell() override;

        void seek(int64_t offset, int whence) override;

        size_t read_buffer(void *output, size_t len) override;

    protected:
        char *data;
        const char *ptr;
    };

    class SimpleFile : public tokenizer::DataReader
    {
    public:
        SimpleFile(const std::string &path);
        ~SimpleFile();

        int64_t tell() override;

        void seek(int64_t offset, int whence) override;

        size_t read_buffer(void *output, size_t len) override;
    protected:
        FILE *f;
    };

    class TensorInfo
    {
    public:
        TensorInfo(ggml::type type, int n_dim, const int64_t *ne, size_t _offset, const char *name);
        ~TensorInfo();

        bool load(tokenizer::DataReader *reader, LayerBufAllocator *alloc, ggml::type target_type);

        size_t read_tensor_data(tokenizer::DataReader *reader, size_t read_offset, size_t write_offset, size_t data_size, ggml::type target_type);

        size_t aligned_data_start(size_t offset);
        size_t aligned_size(void);

        size_t get_nbytes(void);

        void assign_to(ggml::tensor *tensor);

    protected:
        size_t read_tensor_data_f32_f16(tokenizer::DataReader *reader, size_t read_offset, size_t write_offset, size_t data_size);
        size_t read_tensor_data_f16_f32(tokenizer::DataReader *reader, size_t read_offset, size_t write_offset, size_t data_size);

    public:
        ggml::tensor tensor;
        const size_t _offset;
        BackendBufAllocator::Usage usage;
        BackendBuffer *data;
        LayerBufAllocator *alloc;
        ggml::type original_type;
    };

    class ModelLoader
    {
    public:
        ModelLoader(const std::string &path)
            : ModelLoader(new SimpleFile(path))
        {
        }

        int64_t tell() const
        {
            return _file->tell();
        }

        void seek(int64_t offset, int whence)
        {
            _file->seek(offset, whence);
        }

        template <typename T> T read_basic()
        {
            return _file->read_basic<T>();
        }

        std::string read_string(size_t length)
        {
            std::string s;
            s.resize(length);
            _file->read_buffer(s.data(), length);
            return s;
        }

        void set_allocator_manager(LayerAllocatorManager *alloc_manager)
        {
            this->alloc_manager = alloc_manager;
        }

        void read_tensor(const std::string &name, ggml::tensor *tensor);
        void read_tensor(const std::string &name,
                        const std::string &layer_prefix, int num, const std::string &suffix,
                        ggml::tensor *tensor);

        void load_all_tensors(void);

        tokenizer::DataReader *get_reader()
        {
            return _file.get();
        }

    protected:
        void read_tensor(const std::string &name, ggml::tensor *tensor, LayerBufAllocator *allocator);
        void read_tensor(const std::string &name,
                        const std::string &layer_prefix, int num, const std::string &suffix,
                        ggml::tensor *tensor, LayerBufAllocator *allocator);

        void read_tensor(const std::string &name,
                         const std::vector<std::string> &concat_list, ggml::tensor *tensor, LayerBufAllocator *allocator);

    private:
        ModelLoader(tokenizer::DataReader *mapped_file)
            : _file(std::unique_ptr<tokenizer::DataReader>(mapped_file)),
              offset_config(0),
              offset_tokenizer(0),
              model_type(-1), version(-1),
              ff(FileFormat::Unknown)
        {
        }

        std::unique_ptr<tokenizer::DataReader> _file;

    public:
        size_t offset_config;
        size_t offset_tokenizer;
        int model_type;
        int version;
        std::map<std::string, TensorInfo> tensor_dict;
    protected:
        LayerAllocatorManager *alloc_manager = nullptr;

    public:
        enum FileFormat
        {
            Unknown,
            GGML,
            GGMM,
        };

        static std::string ff_to_str(FileFormat f)
        {
            switch (f)
            {
            case FileFormat::GGML:
                return "GGML";
            case FileFormat::GGMM:
                return "GGMM";
            default:
                return "??";
            }
        }

        struct GGMMHeader
        {
            uint32_t offset_config;
            uint32_t offset_tokenizer;
            uint32_t offset_tensors;
        };
        GGMMHeader ggml_header;
        FileFormat ff;
        std::string meta;
        std::string model_name;
        std::string model_native_name;
    };

    // ===== generation =====

    struct GenerationConfig
    {
        int max_length;
        int max_context_length;
        bool do_sample;
        bool reversed_role;
        int top_k;
        float top_p;
        float temperature;
        float presence_penalty;
        float tfs_z;
        std::string sampling;
        std::string ai_prefix;
        std::string dump_dot;
        std::string emb_rank_query_sep;

        GenerationConfig()
        {
        }

        GenerationConfig(int max_length, int max_context_length, bool do_sample, bool reversed_role,
                         int top_k,
                         float top_p, float temperature, int num_threads, const std::string sampling, float presence_penalty, float tfs_z)
            : max_length(max_length), max_context_length(max_context_length),
              do_sample(do_sample), reversed_role(reversed_role), top_k(top_k),
              top_p(top_p), temperature(temperature), presence_penalty(presence_penalty), tfs_z(tfs_z),
              sampling(sampling), ai_prefix("") {}

        void set_ai_prefix(const std::string &prefix);
    };

    class ModelPerfInfo
    {
    public:
        enum Type
        {
            Prompt = 0,
            Generation,
            NUM
        };

        struct Performance
        {
            size_t tok_count;
            double duration_ms;
        };

        ModelPerfInfo();

        void Reset(void);
        double Elapsed(void);

        void Accumulate(Type type, size_t tok_count);

        Performance timings[Type::NUM];

    private:
        using Clock = std::chrono::steady_clock;
        using MilliSecond = std::chrono::duration<double, std::ratio<1, 1000>>;

        std::chrono::time_point<Clock> m_beg { Clock::now() };
    };

    class ModelSessionMemory
    {
    public:
        ModelSessionMemory();

        void *prepare_buffer(int id, size_t size);
        void *get_buffer(int id, size_t *size = nullptr);

        void set_n_past(int n_past);
        void set_n_past_offset(int n_past_offset);

        int get_n_past(void) const;
        int get_n_past_offset(void) const;

        void copy_from(const ModelSessionMemory &sess);

        void dump(const char *fn);

    private:
        void prepare(int id);
        std::vector<std::vector<uint8_t>> buffers;
        int n_past;
        int n_past_offset;
    };

    class AbstractModel
    {
    public:
        virtual ~AbstractModel()
        {}

        virtual void set_layer_ids(const std::vector<int> &ids) = 0;

        virtual std::vector<int> generate(const std::vector<int> &input_ids, const GenerationConfig &gen_config,
                                            const bool continuous,
                                            bool &completed,
                                            ModelPerfInfo *performance,
                                            int gen_max_tokens,
                                            BaseStreamer *streamer = nullptr) = 0;

        virtual bool generate_next_token(const std::vector<int> &input_ids, const GenerationConfig &gen_config, std::vector<float> &lm_logits) { return true; };

        virtual void abort_generation(void) = 0;

        virtual void text_embedding(const GenerationConfig &gen_config, const std::vector<int> &input_ids,
                                    std::vector<float> &embedding) = 0;

        virtual float qa_rank(const GenerationConfig &gen_config,
                              const std::vector<int> &input_ids) = 0;

        virtual int get_text_embedding_dim(void) const = 0;

        virtual std::string type_name() const = 0;
        virtual std::string native_name() const = 0;
        virtual ModelPurpose get_purpose() const = 0;
        virtual void set_names(const std::string &name, const std::string &native_name) = 0;

        virtual void load(ModelLoader &loader) = 0;

        virtual void set_tokenizer(BaseTokenizer *tokenizer) = 0;

        virtual void set_ctx(int n_ctx)= 0;

        virtual void seed(int x) = 0;

        virtual int get_max_length(void) = 0;

        virtual int get_n_past(void) = 0;
        virtual void set_n_past(int n_past) = 0;

        virtual void shift_memory(int keep) = 0;

        virtual int save_session(FILE *f) const = 0;
        virtual int load_session(FILE *f) = 0;

        virtual int save_session(ModelSessionMemory &session) const = 0;
        virtual int load_session(ModelSessionMemory &session) = 0;

        virtual int64_t get_param_num(bool effective_only) const = 0;

        virtual ChunkInterceptor *get_interceptor(void) { return nullptr; }

        virtual void set_additional_args(const std::map<std::string, std::string> &args) {}

        virtual LayerAllocatorManager *get_alloc_manager(void) = 0;
    };

    class ModelProxy : public AbstractModel
    {
    public:
        ModelProxy() : AbstractModel(), model(nullptr) {}

        virtual ~ModelProxy()
        {
            delete model;
        }

        void set_layer_ids(const std::vector<int> &ids) override { return model->set_layer_ids(ids); }

        std::vector<int> generate(const std::vector<int> &input_ids, const GenerationConfig &gen_config,
                                            const bool continuous,
                                            bool &completed,
                                            ModelPerfInfo *performance,
                                            int gen_max_tokens,
                                            BaseStreamer *streamer = nullptr) override
        {
            return model->generate(input_ids, gen_config, continuous, completed, performance, gen_max_tokens, streamer);
        }

        bool generate_next_token(const std::vector<int> &input_ids, const GenerationConfig &gen_config, std::vector<float> &lm_logits) override
        {
            return model->generate_next_token(input_ids, gen_config, lm_logits);
        }

        void abort_generation(void) override { model->abort_generation(); }

        void text_embedding(const GenerationConfig &gen_config, const std::vector<int> &input_ids,
                                    std::vector<float> &embedding) override
        {
            model->text_embedding(gen_config, input_ids, embedding);
        }

        float qa_rank(const GenerationConfig &gen_config,
                              const std::vector<int> &input_ids) override { return model->qa_rank(gen_config, input_ids); }

        int get_text_embedding_dim(void) const override { return model->get_text_embedding_dim(); }

        std::string type_name() const  override { return model->type_name(); }
        std::string native_name() const  override { return model->native_name(); }
        ModelPurpose get_purpose() const  override { return model->get_purpose(); }
        void set_names(const std::string &name, const std::string &native_name) override { model->set_names(name, native_name); }

        void load(ModelLoader &loader) override { return model->load(loader); }

        void set_tokenizer(BaseTokenizer *tokenizer) override { model->set_tokenizer(tokenizer); }

        void set_ctx(int n_ctx) override { model->set_ctx(n_ctx); }

        void seed(int x) override { model->seed(x); }

        int get_max_length(void) override { return model->get_max_length(); }

        int get_n_past(void) override { return model->get_n_past(); }
        void set_n_past(int n_past) override { model->set_n_past(n_past); }

        void shift_memory(int keep) override { model->shift_memory(keep); }

        int save_session(FILE *f) const override { return model->save_session(f); }
        int load_session(FILE *f) override { return model->load_session(f); }

        int save_session(ModelSessionMemory &session) const override { return model->save_session(session); }
        int load_session(ModelSessionMemory &session) override { return model->load_session(session); }

        int64_t get_param_num(bool effective_only) const override { return model->get_param_num(effective_only); }

        ChunkInterceptor *get_interceptor(void) override { return model->get_interceptor(); }

        LayerAllocatorManager *get_alloc_manager(void) override
        {
            return model->get_alloc_manager();
        }

    protected:
        AbstractModel *model;
        void set_proxy_model(AbstractModel *model) { this->model = model; }
    };

    class BaseModel : public AbstractModel
    {
    public:
        BaseModel(int type, ModelPurpose purpose) :
            type_(type), n_past(0),
            n_past_offset(0), tokenizer(nullptr),
            purpose(purpose), aborted(false)
        {}

        virtual ~BaseModel()
        {}

        std::vector<int> generate(const std::vector<int> &input_ids, const GenerationConfig &gen_config,
                                            const bool continuous,
                                            bool &completed,
                                            ModelPerfInfo *performance,
                                            int gen_max_tokens,
                                            BaseStreamer *streamer = nullptr) override
        {
            std::vector<int> r;
            return r;
        }

        void abort_generation(void) override
        {
            aborted = true;
        }

        static bool is_aborted(void * data)
        {
            BaseModel *p = reinterpret_cast<BaseModel *>(data);
            return p->aborted;
        }

        void text_embedding(const GenerationConfig &gen_config, const std::vector<int> &input_ids,
                                    std::vector<float> &embedding) override
        {}

        float qa_rank(const GenerationConfig &gen_config,
                              const std::vector<int> &input_ids) override
        {
            return 0.0f;
        }

        int get_text_embedding_dim(void) const override { return -1; }

        std::string type_name() const override { return name_; }
        std::string native_name() const override { return native_name_; }
        ModelPurpose get_purpose() const override { return purpose; }

        void set_names(const std::string &name, const std::string &native_name) override
        {
            name_ = name;
            native_name_ = native_name;
        }

        void set_tokenizer(BaseTokenizer *tokenizer) override
        {
            this->tokenizer = tokenizer;
        }

        void set_ctx(int n_ctx) override {}

        void seed(int x) override { _seed = x; }

        int get_n_past(void) override { return n_past; }

        void set_n_past(int n_past) override
        {
            this->n_past = n_past;
            n_past_offset = 0;
        }

        void shift_memory(int keep) override
        {
            CHATLLM_CHECK(n_past >= keep) << "length of kept should not exceeds history";

            n_past_offset += n_past - keep;
            n_past = keep;
        }

        int64_t get_param_num(bool effective_only) const override
        {
            return 0;
        }

        int save_session(FILE *f) const override;
        int load_session(FILE *f) override;

        int save_session(ModelSessionMemory &session) const override;
        int load_session(ModelSessionMemory &session) override;

    private:
        struct state
        {
            int type;
            int n_past;
            int n_past_offset;
        };
    protected:
        const int type_;
        std::string name_;
        std::string native_name_;
        int _seed;
        int n_past;
        int n_past_offset;
        BaseTokenizer *tokenizer;
        ModelPurpose purpose;
        bool aborted;
    };

    class ModelObject
    {
    public:
        struct extra_args
        {
            int   max_length;
            std::string layer_spec;
            std::string gpu_layers;
            bool moe_on_cpu;
            int n_threads;
            int batch_size;
            std::string cache_type;
            extra_args(int max_length, const std::string &layer_spec, const std::string &gpu_layers, bool moe_on_cpu, int n_threads, int batch_size, const std::string &cache_type)
                : max_length(max_length), layer_spec(layer_spec), gpu_layers(gpu_layers), moe_on_cpu(moe_on_cpu), n_threads(n_threads),
                  batch_size(batch_size), cache_type(cache_type)
            {}
            extra_args() : extra_args(-1, "", "", false, 1, 0, "") {}
        };

        ModelObject(const std::string &path);
        ModelObject(const std::string &path, const extra_args &args);

        void text_embedding(const std::string &input, const GenerationConfig &gen_config, std::vector<float> &result);

        AbstractModel *fork_model(const extra_args &args);

    public:
        std::unique_ptr<BaseTokenizer> tokenizer;
        std::unique_ptr<AbstractModel> model;
        std::unique_ptr<ModelLoader> loader;
        const bool loaded;
    };

    class ModelFactory
    {
    public:
        struct Result
        {
            std::unique_ptr<BaseTokenizer> tokenizer;
            std::unique_ptr<AbstractModel> model;
        };

        static bool load(ModelLoader &loader, Result &result, const ModelObject::extra_args &args);

        static AbstractModel *load_model_again(ModelLoader &loader, const ModelObject::extra_args &args);

        static std::string load_info(ModelLoader &loader);

    private:
        static bool load(int model_type, int version, ModelLoader &loader, Result &result, const ModelObject::extra_args &args);
    };

    class Pipeline
    {
    public:
        enum ExtendingMethod
        {
            Shift,
            Restart,
            None,
        };

        Pipeline(const std::string &path);
        Pipeline(const std::string &path, const ModelObject::extra_args &args);

        virtual ~Pipeline() {};

        virtual std::string chat(Messages &history, const GenerationConfig &gen_config,
                         BaseStreamer *streamer = nullptr);
        virtual void abort_generation(void);
        virtual void eval_sys_prompt(const GenerationConfig &gen_config);

        std::string chat_continue(Messages &history, const std::string &external, const GenerationConfig &gen_config,
                         BaseStreamer *streamer = nullptr);

        void set_system_prompt(const std::string &prompt);
        void set_extending_method(ExtendingMethod method);
        virtual void set_additional_args(const std::map<std::string, std::string> &args);

        void text_embedding(const std::string &input, const GenerationConfig &gen_config, std::vector<float> &result);
        void text_tokenize(const std::string &input, const GenerationConfig &gen_config, std::vector<int> &result);
        float qa_rank(const std::string &q, const std::string &a, const GenerationConfig &gen_config);

        int get_text_embedding_dim(void);

        virtual std::string get_additional_description(void) const;

        bool is_loaded(void) const { return modelobj.loaded; }

        virtual void restart(void);
        virtual void rewind(int n_past);

        virtual int save_session(const Messages &history, const std::string &file_name);
        virtual int load_session(Messages &history, const std::string &file_name, BaseStreamer *streamer, int *n_past = nullptr);
    protected:
        const char head_magic[18] = "CHATLLM-SESSION\x00\x01";

        struct file_header
        {
            char magic[16];
            size_t history_len;
        };

    public:
        BaseTokenizer *tokenizer;
        AbstractModel *model;
        ModelPerfInfo performance;
        int gen_max_tokens;

    protected:
        bool initializing;
        ExtendingMethod extending;
        ModelObject modelobj;

        void add_ai_prefix(std::vector<int> &input_ids, const GenerationConfig &gen_config, BaseStreamer *streamer);

        virtual std::string chat_with_ext_completion(Messages &history, const std::string &external, const GenerationConfig &gen_config,
                         BaseStreamer *streamer);
        virtual std::string chat_with_restart(const Messages &history, const GenerationConfig &gen_config,
                         BaseStreamer *streamer);
        virtual std::string chat_with_shift(const Messages &history, const GenerationConfig &gen_config,
                         BaseStreamer *streamer);
        virtual std::string chat_without_extending(const Messages &history, const GenerationConfig &gen_config,
                               BaseStreamer *streamer);

        virtual void before_chat(Messages &history, const GenerationConfig &gen_config, BaseStreamer *streamer);
        virtual void post_chat(Messages &history, const GenerationConfig &gen_config, BaseStreamer *streamer);
    };

    class BeamSearchPipeline: public Pipeline
    {
    public:
        BeamSearchPipeline(const std::string &path, const ModelObject::extra_args &args, int num_beams);

        std::string chat(Messages &history, const GenerationConfig &gen_config,
                         BaseStreamer *streamer = nullptr) override;
    private:
        void do_chat(Messages &history, const GenerationConfig &gen_config, BaseStreamer *streamer);
        void do_chat0(Messages &history, const GenerationConfig &gen_config, BaseStreamer *streamer);

        class Beam
        {
        public:
            Beam(int vocab_size, int max_length, BaseTokenizer *tokenizer);
            void clear(const std::vector<int> &init_ids);
            void add(int token_id, float score);
            int get_last_token(void) const;
            void refresh_scores(void);
            void set_max_length(int max_length);
            ModelSessionMemory session;
            std::vector<float> scores;
            std::vector<int> trace;
            float score;
            bool completed;
            int max_length;
            BaseTokenizer *tokenizer;
        };
        std::vector<Beam> beams;
    };

    class AugmentedQueryComposer
    {
    public:
        AugmentedQueryComposer();
        std::string compose_augmented_query(const std::string &query, const std::vector<std::string> augments) const;
        std::string rewrite_query_for_retrieve(const std::string &query) const;
        std::string parse_rewritten_query_result(const std::string &query) const;
        bool is_rewritten_template_set(void) const;
        void set_prompt_template(const std::string &s);
        void set_context_sep(const std::string &s);
        void set_rewrite_template(const std::string &s);
    protected:
        std::string prompt_template;
        std::string context_sep;
        std::string query_rewritten_template;
    };

    class VectorStores
    {
    public:
        VectorStores(DistanceStrategy vec_cmp, const std::map<std::string, std::vector<std::string>> &vector_stores);
        ~VectorStores();

        bool select(const std::string &name);

        CVectorStore *get(const std::string &name);
        CVectorStore *get();

    protected:
        std::map<std::string, CVectorStore *> stores;
        CVectorStore *def_store;
    };

    class RAGPipeline : public Pipeline
    {
    public:
        RAGPipeline(const std::string &path, const ModelObject::extra_args &args,
                    DistanceStrategy vec_cmp, const std::map<std::string, std::vector<std::string>> &vector_stores,
                    const std::string &embedding_model, const std::string &reranker_model = "");

        ~RAGPipeline() override {}

        std::string get_additional_description(void) const override;

        bool select_vector_store(const std::string &name);

        AugmentedQueryComposer composer;
        bool    hide_reference;
        int     retrieve_top_n;
        int     rerank_top_n;
        bool    dump;
        float   rerank_score_threshold;
        int     rag_post_extending;
        bool    rerank_rewrite;

    protected:
        void before_chat(Messages &history, const GenerationConfig &gen_config, BaseStreamer *streamer) override;
        void post_chat(Messages &history, const GenerationConfig &gen_config, BaseStreamer *streamer) override;

        VectorStores vs;
        ModelObject embedding;
        std::unique_ptr<ModelObject> reranker;
        std::vector<std::string> metainfo;

    private:
        void rerank(const std::string &query, std::vector<int64_t> &candidates, const GenerationConfig &gen_config, int top_n = 3);
        std::string rewrite_query(const std::string &prompt, const GenerationConfig &gen_config);
        std::unique_ptr<AbstractModel> rewrite_model;
    };

} // namespace chatllm
