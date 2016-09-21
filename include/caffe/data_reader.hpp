#ifndef CAFFE_DATA_READER_HPP_
#define CAFFE_DATA_READER_HPP_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "caffe/common.hpp"
#include "caffe/internal_thread.hpp"
#include "caffe/util/blocking_queue.hpp"
#include "caffe/util/db.hpp"

namespace caffe {

/**
 * @brief Reads data from a source to queues available to data layers.
 * A single reading thread is created per source, even if multiple solvers
 * are running in parallel, e.g. for multi-GPU training. This makes sure
 * databases are read sequentially, and that each solver accesses a different
 * subset of the database. Data is distributed to solvers in a round-robin
 * way to keep parallel training deterministic.
 */
class DataReader {
 public:
  explicit DataReader(const LayerParameter& param);
  ~DataReader();

  inline BlockingQueue<std::string*>& free() const {
    return queue_pair_->free_;
  }
  inline BlockingQueue<std::string*>& full() const {
    return queue_pair_->full_;
  }

 protected:
  // Queue pairs are shared between a body and its readers
  class QueuePair {
   public:
    explicit QueuePair(int size);
    ~QueuePair();

    BlockingQueue<std::string*> free_;
    BlockingQueue<std::string*> full_;

  DISABLE_COPY_AND_ASSIGN(QueuePair);
  };

  class DBWrapper  {
   public:
    explicit DBWrapper(const LayerParameter& param);
    virtual string value() = 0;
    virtual void Next() = 0;
   protected:
    shared_ptr<db::DB> db;
    shared_ptr<db::Cursor> cursor;
  };

  class DBShuffle: public DBWrapper {
   public:
    explicit DBShuffle(const LayerParameter& param);
    virtual string value() {
      return string(static_cast<const char*>(current_image_->first),
                                                      current_image_->second);
    }
    virtual void Next();
   protected:
    vector<std::pair<void*, int> > image_pointers_;
    vector<std::pair<void*, int> >::iterator current_image_;
    shared_ptr<Caffe::RNG> prefetch_rng_;

    void ShuffleImages();
  };

  class DBSequential: public DBWrapper {
   public:
    explicit DBSequential(const LayerParameter& param): DBWrapper(param)  {}
    virtual string value()  { return cursor->value(); }
    virtual void Next();
  };

  // A single body is created per source
  class Body : public InternalThread {
   public:
    explicit Body(const LayerParameter& param);
    virtual ~Body();

   protected:
    void InternalThreadEntry();
    void read_one(DBWrapper* img, QueuePair* qp);
    void ShuffleImages();

    const LayerParameter param_;
    BlockingQueue<shared_ptr<QueuePair> > new_queue_pairs_;

    friend class DataReader;

  DISABLE_COPY_AND_ASSIGN(Body);
  };

  // A source is uniquely identified by its layer name + path, in case
  // the same database is read from two different locations in the net.
  static inline string source_key(const LayerParameter& param) {
    return param.name() + ":" + param.data_param().source();
  }

  const shared_ptr<QueuePair> queue_pair_;
  shared_ptr<Body> body_;

  static map<const string, boost::weak_ptr<DataReader::Body> > bodies_;

DISABLE_COPY_AND_ASSIGN(DataReader);
};

}  // namespace caffe

#endif  // CAFFE_DATA_READER_HPP_
