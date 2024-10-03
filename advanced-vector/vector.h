#pragma once
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <new>
#include <utility>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept {
        Swap(other);
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;
    
    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_;
    }

    Vector() = default;

    Vector(size_t size)
        : data_(size)
        , size_(size)
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size_);
    }

    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_)  //
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                /* Применить copy-and-swap */
                Vector copy_v(rhs);
                Swap(copy_v);
            }
            else {
                /* Скопировать элементы из rhs, создав при необходимости новые
                   или удалив существующие */
                if (rhs.size_ < size_) {
                    std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + rhs.size_, data_.GetAddress());
                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                }
                else {
                    std::copy(rhs.data_.GetAddress(), rhs.data_.GetAddress() + size_, data_.GetAddress());
                    std::uninitialized_copy_n(rhs.data_.GetAddress() + size_, rhs.size_ - size_, data_.GetAddress() + size_);
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    void Resize(size_t new_size) {
        if (new_size > data_.Capacity()) {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            size_ = new_size;
        }
        else {
            if (new_size < size_) {
                std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
            }
            else {
                std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            }
            size_ = new_size;
        }
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        size_t pos_num = std::distance(begin(), const_cast<iterator>(pos));
        assert(pos_num <= size_); // место вставки не должно превышать size_
        iterator ptr = nullptr;
        // ВЫДЕЛЯЕМ НОВУЮ ПАМЯТЬ
        if (size_ == data_.Capacity()) {
            if (size_ == 0) {
                RawMemory<T> new_data(1);
                new (new_data.GetAddress()) T(std::forward<Args>(args)...);
                data_.Swap(new_data);
                ptr = begin();
            }
            else {
                RawMemory<T> new_data(size_ * 2);
                new (new_data.GetAddress() + pos_num) T(std::forward<Args>(args)...);
                MoveOrCopyBeforeAndAfterElements(new_data, pos_num);
                std::destroy_n(begin(), size_);
                data_.Swap(new_data);
                ptr = begin() + pos_num;
            }
        }
        // НЕ ВЫДЕЛЯЕМ НОВУЮ ПАМЯТЬ
        else {
            if (pos_num == size_) { // в том числе, если size_ == 0; больше чем size_ он быть не может
                ptr = new (begin() + pos_num) T(std::forward<Args>(args)...);
            }
            else {
                // добавляем со сдвигом вправо
                T buf(std::forward<Args>(args)...); // копируем элемент (он может оказаться элементом этого же вектора)
                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                    std::uninitialized_move(end() - 1, end(), end());
                }
                else {
                    std::uninitialized_copy(end() - 1, end(), end());
                }
                try {
                    std::move_backward(begin() + pos_num, end() - 1, end());
                }
                catch (...) {
                    std::destroy_at(end());
                    throw;
                }
                *(begin() + pos_num) = std::move(buf);
                ptr = begin() + pos_num;
            }
        }
        ++size_;
        return ptr;
    }

    void MoveOrCopyBeforeAndAfterElements(RawMemory<T>& new_data, size_t pos_num) {
        // перемещаем/копируем элементы, которые были перед
        try {
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(begin(), pos_num, new_data.GetAddress());
            }
            else {
                std::uninitialized_copy_n(begin(), pos_num, new_data.GetAddress());
            }
        }
        catch (...) {
            // если падает на копировании элементов "перед", надо уничтожить pos-элемент
            std::destroy_at(new_data.GetAddress() + pos_num);
            throw;
        }
        // перемещаем/копируем элементы, которые были после
        try {
            if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move_n(begin() + pos_num, size_ - pos_num, new_data.GetAddress() + pos_num + 1);
            }
            else {
                std::uninitialized_copy_n(begin() + pos_num, size_ - pos_num, new_data.GetAddress() + pos_num + 1);
            }
        }
        catch (...) {
            // если падает на копировании элементов "после", тогда удаляет и элементы "перед", и pos-элемент
            std::destroy_n(new_data.GetAddress(), pos_num);
            std::destroy_at(new_data.GetAddress() + pos_num);
            throw;
        }
    }

    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        size_t pos_num = std::distance(begin(), const_cast<iterator>(pos));
        std::destroy_at(begin() + pos_num);
        std::move(begin() + pos_num + 1, end(), begin() + pos_num);
        --size_;
        return begin() + pos_num;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(end(), std::forward<Args>(args)...);
    }

    void PopBack() /* noexcept */ {
        if (size_ > 0) {
            --size_;
            std::destroy_at(data_.GetAddress() + size_);
        }
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);

        // constexpr оператор if будет вычислен во время компиляции
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }

        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};