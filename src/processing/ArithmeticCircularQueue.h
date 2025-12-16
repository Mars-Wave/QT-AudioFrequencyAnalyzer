#pragma once
#include <QMetaType>

template <class X>
class ArithmeticCircularQueue
{
public:
    ArithmeticCircularQueue() {}

    ArithmeticCircularQueue(int size): _capacity(size) {
        _arr = new X[size];
        _indexes = new int[size];
    }

    ~ArithmeticCircularQueue() {
        delete[] _arr    ; _arr     = nullptr;
        delete[] _indexes; _indexes = nullptr;
    }

    ArithmeticCircularQueue(const ArithmeticCircularQueue<X>& other)
        : _capacity(other._capacity),
        _front(other._front),
        _rear(other._rear),
        _count(other._count),
        _sumX(other._sumX),
        _meanX(other._meanX),
        _sumOfSquaresX(other._sumOfSquaresX),
        _defaultValue(other._defaultValue),
        _arr(nullptr),
        _indexes(nullptr)
    {
        if (_capacity > 0) {
            _arr = new X[_capacity];
            for (int i = 0; i < _capacity; ++i) {
                _arr[i] = other._arr[i];
            }
            _indexes = new int[_capacity];
            for (int i = 0; i < _capacity; ++i) {
                _indexes[i] = other._indexes[i];
            }
        }
    }

    ArithmeticCircularQueue<X>& operator=(const ArithmeticCircularQueue<X>& other)
    {
        if (this == &other) {
            return *this;
        }

        delete[] _arr;
        _arr = nullptr;
        delete[] _indexes;
        _indexes = nullptr;

        _capacity = other._capacity;
        _front = other._front;
        _rear = other._rear;
        _count = other._count;
        _sumX = other._sumX;
        _meanX = other._meanX;
        _sumOfSquaresX = other._sumOfSquaresX;
        _defaultValue = other._defaultValue;

        if (_capacity > 0) {
            _arr = new X[_capacity];
            for (int i = 0; i < _capacity; ++i) {
                _arr[i] = other._arr[i];
            }
            _indexes = new int[_capacity];
            for (int i = 0; i < _capacity; ++i) {
                _indexes[i] = other._indexes[i];
            }
        }
        return *this;
    }
    inline void resize(int size);

    inline void clear();

    inline void dequeue();
    inline void enqueue(X x);
    inline int size() const { return _count; }
    inline bool isEmpty() const;
    inline bool isFull() const;

    inline const X& peek() const;
    inline const X& data(int idx) { return _arr[idx]; }

    inline void prepareIndexes();
    inline int* index(int idx) { return &_indexes[idx]; }
    inline const X& getMean()         ;
    inline const X& getSum()          ;
    inline const X& getSumOfSquares() ;

protected:
    int _capacity = 0;  // maximum capacity of the queue
    int _front = 0;     // front points to front element in the queue (if any)
    int _rear = -1;     // rear points to last element in the queue
    int _count = 0;     // current size of the queue
    X   _sumX          = {};
    X   _meanX         = {};
    X   _sumOfSquaresX = {};

    X   _defaultValue  = X{};
    X   *_arr = nullptr;     // array to store queue elements
    int *_indexes = nullptr; // array with indexes to iterate in one go
};


//-----------------------------------------------------------------------------

template<class X>
inline const X& ArithmeticCircularQueue<X>::getSum()
{
    return _sumX;
}

//-----------------------------------------------------------------------------

template<class X>
inline const X& ArithmeticCircularQueue<X>::getSumOfSquares()
{
    return _sumOfSquaresX;
}

//-----------------------------------------------------------------------------

template<class X>
inline const X& ArithmeticCircularQueue<X>::getMean()
{
    if (_count != 0) {
        _meanX = getSum() / static_cast<X>(_count); //MUST BE CONSTRUCTIBLE LIKE THIS: X(int a)
    }
    return _meanX;
}

//-----------------------------------------------------------------------------

template<class X>
void ArithmeticCircularQueue<X>::resize(int size)
{
    if (size == _capacity) { return; }

    if (_arr)     { delete[] _arr; }
    if (_indexes) { delete[] _indexes; }

    _arr     = size > 0 ? new X[size] : nullptr;
    _indexes = size > 0 ? new int[size] : nullptr;

    _capacity = size;
    _count = 0;
    _sumX = X{};
    _sumOfSquaresX = X{};
    _meanX = X{};
    _front = 0;
    _rear = -1;
}

//-----------------------------------------------------------------------------

template<class X>
void ArithmeticCircularQueue<X>::clear() {
    _front = 0;
    _rear = -1;
    _count = 0;
    _sumX = X{};
    _sumOfSquaresX = X{};
    _meanX = X{};
}

//-----------------------------------------------------------------------------

template <class X>
void ArithmeticCircularQueue<X>::dequeue()
{
    // check for queue underflow
    if (isEmpty()) { return; }

    //cout << "Removing " << arr[front] << '\n';
    _sumX  -= _arr[_front];
    _sumOfSquaresX -= _arr[_front] * _arr[_front];
    _front = (_front + 1) % _capacity;
    _count--;
}

//-----------------------------------------------------------------------------
template <class X>
void ArithmeticCircularQueue<X>::enqueue(X item)
{
    // check for queue overflow
    if (isFull()) {
        dequeue();
    }

    //cout << "Inserting " << item << '\n';
    if (_capacity == 0) return;

    _rear = (_rear + 1) % _capacity;
    _arr[_rear] = item;
    _count++;
    _sumX += _arr[_rear];
    _sumOfSquaresX += _arr[_front] * _arr[_front];
}

//-----------------------------------------------------------------------------
template <class X>
const X& ArithmeticCircularQueue<X>::peek() const
{
    if (isEmpty()) { return _defaultValue; }
    return _arr[_front];
}

//-----------------------------------------------------------------------------

template<class X>
void ArithmeticCircularQueue<X>::prepareIndexes()
{
    if (!_count) { return; }

    if (_front <= _rear) {
        int i = 0;
        for (int idx = _front; idx <= _rear; ++idx) { _indexes[i] = idx; ++i; }
    } else {
        int i = 0;
        for (int idx = _front; idx < _capacity; ++idx) { _indexes[i] = idx; ++i; }
        for (int idx = 0; idx <= _rear; ++idx) { _indexes[i] = idx; ++i; }
    }
}

//-----------------------------------------------------------------------------
template <class X>
bool ArithmeticCircularQueue<X>::isEmpty() const
{
    return (_count == 0);
}

//-----------------------------------------------------------------------------
template <class X>
bool ArithmeticCircularQueue<X>::isFull() const
{
    return (_count == _capacity);
}

Q_DECLARE_METATYPE(ArithmeticCircularQueue<float>);
