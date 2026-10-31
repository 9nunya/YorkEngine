#ifndef YORK_UNIFORM_BUFFER_HXX
#define YORK_UNIFORM_BUFFER_HXX

#include <cstring>
#include <utility>
#include "render/device.hxx"
#include "render/shader.hxx"
#include "util/vulkan.hxx"

namespace york::types {
    struct UniformBufferMeta {
        uint32_t binding;
        render::ShaderStage stage;
    };

    template <typename Struct>
    class UniformBuffer {
    public:
        explicit UniformBuffer(render::Device &device)
            : device_(device) {
            createBuffer();
        }

        void update(const Struct &newData) {
            void *mapped = memory_.mapMemory(0, sizeof(Struct));
            byteSize_ = sizeof(Struct);
            std::memcpy(mapped, &newData, sizeof(Struct));
            memory_.unmapMemory();
        };

        [[nodiscard]] vk::Buffer getBufferHandle() const
            { return *buffer_; }
        [[nodiscard]] size_t byteSize() const
            { return byteSize_; }

        static_assert(std::is_trivially_copyable_v<Struct>);
    private:
        vk::raii::DeviceMemory memory_{nullptr};
        vk::raii::Buffer buffer_{nullptr};
        VkDeviceSize byteSize_{sizeof(Struct)};
        render::Device &device_;

        void createBuffer() {
            auto allocation = util::vulkan::createBuffer(
                device_,
                sizeof(Struct),
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible
                | vk::MemoryPropertyFlagBits::eHostCoherent);
            memory_ = std::move(allocation.memory);
            buffer_ = std::move(allocation.buffer);
        }
    };

    class RegisteredUniformBase {
    public:
        virtual ~RegisteredUniformBase() = default;

        [[nodiscard]] virtual const UniformBufferMeta& meta() const = 0;
        [[nodiscard]] virtual vk::Buffer bufferForFrame(uint32_t frame) const = 0;
        [[nodiscard]] virtual vk::DeviceSize byteSize() const = 0;

        virtual void createFrameBuffers(render::Device&, uint32_t frameCount) = 0;
        virtual void upload(uint32_t frame) = 0;
    };

    template <typename T>
    class RegisteredUniform : public RegisteredUniformBase {
    public:
        RegisteredUniform(const UniformBufferMeta meta)
            : meta_{meta} {}

        void setData(const T& value)
            { cpuData_ = value; }
        [[nodiscard]] const UniformBufferMeta &meta() const override
            { return meta_; }
        [[nodiscard]] vk::Buffer bufferForFrame(uint32_t frame) const override
            { return perFrame_.at(frame).getBufferHandle(); }
        [[nodiscard]] vk::DeviceSize byteSize() const override
            { return sizeof(T); }

        void createFrameBuffers(render::Device &device, uint32_t frameCount) override {
            perFrame_.reserve(frameCount);
            for (uint32_t frame = 0; frame < frameCount; ++frame)
                perFrame_.emplace_back(device);
        }

        void upload(uint32_t frame) override {
            perFrame_.at(frame).update(cpuData_);
        }

    private:
        UniformBufferMeta meta_;
        T cpuData_{};
        std::vector<UniformBuffer<T>> perFrame_;
    };

    class UniformRegistry {
        using VecOfRubs = std::vector<std::unique_ptr<RegisteredUniformBase>>;
    public:
        UniformRegistry() = default;

        [[nodiscard]] const VecOfRubs &getUniformBuffers()
            { return uniformBuffers_; }

        template <typename Struct>
        void registerUniformBuffer(const UniformBufferMeta &meta) {
            auto uniform = std::make_unique
                <RegisteredUniform<Struct>>(meta); // heh nice they align
            uniformBuffers_.emplace_back(std::move(uniform));
        }

        template <typename T>
        [[nodiscard]] std::vector<RegisteredUniform<T>*> getUniformBuffers() {
            std::vector<RegisteredUniform<T>*> matches;

            for (const auto& uniform : uniformBuffers_)
                if (auto* typed = dynamic_cast<RegisteredUniform<T>*>(uniform.get()))
                    matches.push_back(typed);

            return matches;
        }

    private:
        VecOfRubs uniformBuffers_{};
    };

}

#endif //YORK_UNIFORM_BUFFER_HXX
