/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD (revised)
license as described in the file LICENSE.
*/

#include "vw_clr.h"
#include "vowpalwabbit.h"
#include "best_constant.h"
#include "parser.h"
#include "hash.h"
#include "vw_example.h"
#include "vw_allreduce.h"
#include "vw_builder.h"
#include "clr_io.h"

using namespace System;
using namespace System::Text;

namespace VW
{
  VowpalWabbit::VowpalWabbit(VowpalWabbitSettings^ settings)
    : VowpalWabbitBase(settings)
  {
    if (settings == nullptr)
    {
      throw gcnew ArgumentNullException("settings");
    }

    if (settings->ParallelOptions != nullptr)
    {
      m_vw->all_reduce_type = AllReduceType::Thread;
      auto total = settings->ParallelOptions->MaxDegreeOfParallelism;

      if (settings->Root == nullptr)
      {
        m_vw->all_reduce = new AllReduceThreads(total, settings->Node);
      }
      else
      {
        auto parent_all_reduce = (AllReduceThreads*)settings->Root->m_vw->all_reduce;

        m_vw->all_reduce = new AllReduceThreads(parent_all_reduce, total, settings->Node);
      }
    }

    try
    {
      m_hasher = GetHasher();
    }
    CATCHRETHROW
  }

  VowpalWabbit::VowpalWabbit(String^ args)
    : VowpalWabbit(gcnew VowpalWabbitSettings(args))
  {
  }

  void VowpalWabbit::Driver()
  {
    try
    {
      LEARNER::generic_driver(*m_vw);
    }
    CATCHRETHROW
  }

  void VowpalWabbit::RunMultiPass()
  {
    if (m_vw->numpasses > 1)
    {
      try
      {
        adjust_used_index(*m_vw);
        m_vw->do_reset_source = true;
        VW::start_parser(*m_vw);
        LEARNER::generic_driver(*m_vw);
        VW::end_parser(*m_vw);
      }
      CATCHRETHROW
    }
  }

  VowpalWabbitPerformanceStatistics^ VowpalWabbit::PerformanceStatistics::get()
  {
    // see parse_args.cc:finish(...)
    auto stats = gcnew VowpalWabbitPerformanceStatistics();

    if (m_vw->current_pass == 0)
    {
      stats->NumberOfExamplesPerPass = m_vw->sd->example_number;
    }
    else
    {
      stats->NumberOfExamplesPerPass = m_vw->sd->example_number / m_vw->current_pass;
    }

    stats->WeightedExampleSum = m_vw->sd->weighted_examples;
    stats->WeightedLabelSum = m_vw->sd->weighted_labels;

    if (m_vw->holdout_set_off || (m_vw->sd->holdout_best_loss == FLT_MAX))
    {
      stats->AverageLoss = m_vw->sd->sum_loss / m_vw->sd->weighted_examples;
    }
    else
    {
      stats->AverageLoss = m_vw->sd->holdout_best_loss;
    }

    float best_constant; float best_constant_loss;
    if (get_best_constant(*m_vw, best_constant, best_constant_loss))
    {
      stats->BestConstant = best_constant;
      if (best_constant_loss != FLT_MIN)
      {
        stats->BestConstantLoss = best_constant_loss;
      }
    }

    stats->TotalNumberOfFeatures = m_vw->sd->total_features;

    return stats;
  }

  uint64_t VowpalWabbit::HashSpace(String^ s)
  {
    auto newHash = m_hasher(s, hash_base);

#ifdef _DEBUG
    auto oldHash = HashSpaceNative(s);
    assert(newHash == oldHash);
#endif

    return (uint32_t)newHash;
  }

  uint64_t VowpalWabbit::HashFeature(String^ s, size_t u)
  {
    auto newHash = m_hasher(s, u) & m_vw->parse_mask;

#ifdef _DEBUG
    auto oldHash = HashFeatureNative(s, u);
    assert(newHash == oldHash);
#endif

    return (uint64_t)newHash;
  }

  uint64_t VowpalWabbit::HashSpaceNative(String^ s)
  {
    auto bytes = System::Text::Encoding::UTF8->GetBytes(s);
    auto handle = GCHandle::Alloc(bytes, GCHandleType::Pinned);

    try
    {
      return VW::hash_space(*m_vw, reinterpret_cast<char*>(handle.AddrOfPinnedObject().ToPointer()));
    }
    CATCHRETHROW
    finally
    {
      handle.Free();
    }
  }

  uint64_t VowpalWabbit::HashFeatureNative(String^ s, size_t u)
  {
    auto bytes = System::Text::Encoding::UTF8->GetBytes(s);
    auto handle = GCHandle::Alloc(bytes, GCHandleType::Pinned);

    try
    {
      return VW::hash_feature(*m_vw, reinterpret_cast<char*>(handle.AddrOfPinnedObject().ToPointer()), u);
    }
    CATCHRETHROW
      finally
    { handle.Free();
    }
  }

  void VowpalWabbit::Learn(VowpalWabbitExample^ ex)
  {
#if _DEBUG
    if (ex == nullptr)
    {
      throw gcnew ArgumentNullException("ex");
    }
#endif

    try
    {
      m_vw->learn(ex->m_example);

      // as this is not a ring-based example it is not free'd
      m_vw->l->finish_example(*m_vw, *ex->m_example);
    }
    CATCHRETHROW
  }

  generic<typename T> T VowpalWabbit::Learn(VowpalWabbitExample^ ex, IVowpalWabbitPredictionFactory<T>^ predictionFactory)
  {
#if _DEBUG
    if (ex == nullptr)
      throw gcnew ArgumentNullException("ex");

    if (nullptr == predictionFactory)
      throw gcnew ArgumentNullException("predictionFactory");
#endif

    try
    {
      m_vw->learn(ex->m_example);

      auto prediction = predictionFactory->Create(m_vw, ex->m_example);

      // as this is not a ring-based example it is not free'd
      m_vw->l->finish_example(*m_vw, *ex->m_example);

      return prediction;
    }
    CATCHRETHROW
  }

  void VowpalWabbit::Predict(VowpalWabbitExample^ ex)
  {
#if _DEBUG
    if (ex == nullptr)
      throw gcnew ArgumentNullException("ex");
#endif
    try
    {
      m_vw->l->predict(*ex->m_example);

      // as this is not a ring-based example it is not free'd
      m_vw->l->finish_example(*m_vw, *ex->m_example);
    }
    CATCHRETHROW
  }

  generic<typename T> T VowpalWabbit::Predict(VowpalWabbitExample^ ex, IVowpalWabbitPredictionFactory<T>^ predictionFactory)
  {
#if _DEBUG
    if (ex == nullptr)
      throw gcnew ArgumentNullException("ex");
#endif

    try
    {
      m_vw->l->predict(*ex->m_example);

      auto prediction = predictionFactory->Create(m_vw, ex->m_example);

      // as this is not a ring-based example it is not free'd
      m_vw->l->finish_example(*m_vw, *ex->m_example);

      return prediction;
    }
    CATCHRETHROW
  }

  VowpalWabbitExample^ VowpalWabbit::ParseLine(String^ line)
  {
#if _DEBUG
    if (line == nullptr)
      throw gcnew ArgumentNullException("line");
#endif

    auto ex = GetOrCreateNativeExample();
    auto bytes = System::Text::Encoding::UTF8->GetBytes(line);
    auto valueHandle = GCHandle::Alloc(bytes, GCHandleType::Pinned);

    try
    {
      try
      {
        VW::read_line(*m_vw, ex->m_example, reinterpret_cast<char*>(valueHandle.AddrOfPinnedObject().ToPointer()));

        // finalize example
        VW::parse_atomic_example(*m_vw, ex->m_example, false);
        VW::setup_example(*m_vw, ex->m_example);

        // remember the input string for debugging purposes
        ex->VowpalWabbitString = line;

        return ex;
      }
      catch (...)
      {
        delete ex;
        throw;
      }
    }
    CATCHRETHROW
    finally
    {
      valueHandle.Free();
    }
  }

  void VowpalWabbit::Learn(String^ line)
  {
#if _DEBUG
    if (String::IsNullOrEmpty(line))
      throw gcnew ArgumentException("lines must not be empty. For multi-line examples use Learn(IEnumerable<string>) overload.");
#endif

    VowpalWabbitExample^ example = nullptr;

    try
    {
      example = ParseLine(line);
      Learn(example);
    }
    finally
    {
      delete example;
    }
  }

  void VowpalWabbit::Predict(String^ line)
  {
#if _DEBUG
    if (String::IsNullOrEmpty(line))
      throw gcnew ArgumentException("lines must not be empty. For multi-line examples use Predict(IEnumerable<string>) overload.");
#endif

    VowpalWabbitExample^ example = nullptr;

    try
    {
      example = ParseLine(line);
      Predict(example);
    }
    finally
    {
      delete example;
    }
  }

  generic<typename TPrediction> TPrediction VowpalWabbit::Learn(String^ line, IVowpalWabbitPredictionFactory<TPrediction>^ predictionFactory)
  {
#if _DEBUG
    if (String::IsNullOrEmpty(line))
      throw gcnew ArgumentException("lines must not be empty. For multi-line examples use Learn(IEnumerable<string>) overload.");
#endif

    VowpalWabbitExample^ example = nullptr;

    try
    {
      example = ParseLine(line);
      return Learn(example, predictionFactory);
    }
    finally
    {
      delete example;
    }
  }

  generic<typename T> T VowpalWabbit::Predict(String^ line, IVowpalWabbitPredictionFactory<T>^ predictionFactory)
  {
#if _DEBUG
    if (String::IsNullOrEmpty(line))
      throw gcnew ArgumentException("lines must not be empty. For multi-line examples use Learn(IEnumerable<string>) overload.");
#endif

    VowpalWabbitExample^ example = nullptr;

    try
    {
      example = ParseLine(line);
      return Predict(example, predictionFactory);
    }
    finally
    { delete example;
    }
  }

  void VowpalWabbit::Learn(IEnumerable<String^>^ lines)
  {
#if _DEBUG
    if (lines == nullptr)
      throw gcnew ArgumentNullException("lines");
#endif

    auto examples = gcnew List<VowpalWabbitExample^>;

    try
    {
      for each (auto line in lines)
      {
        auto ex = ParseLine(line);
        examples->Add(ex);

        Learn(ex);
      }

      auto empty = GetOrCreateNativeExample();
      examples->Add(empty);
      empty->MakeEmpty(this);
      Learn(empty);
    }
    finally
    {
      for each (auto ex in examples)
      {
        delete ex;
      }
    }
  }

  void VowpalWabbit::Predict(IEnumerable<String^>^ lines)
  {
#if _DEBUG
    if (lines == nullptr)
      throw gcnew ArgumentNullException("lines");
#endif

    auto examples = gcnew List<VowpalWabbitExample^>;

    try
    {
      for each (auto line in lines)
      {
        auto ex = ParseLine(line);
        examples->Add(ex);

        Predict(ex);
      }

      auto empty = GetOrCreateNativeExample();
      examples->Add(empty);
      empty->MakeEmpty(this);
      Predict(empty);
    }
    finally
    { for each (auto ex in examples)
    {
      delete ex;
    }
    }
  }

  generic<typename T> T VowpalWabbit::Learn(IEnumerable<String^>^ lines, IVowpalWabbitPredictionFactory<T>^ predictionFactory)
  {
#if _DEBUG
    if (lines == nullptr)
      throw gcnew ArgumentNullException("lines");
#endif

    auto examples = gcnew List<VowpalWabbitExample^>;

    try
    {
      for each (auto line in lines)
      {
        auto ex = ParseLine(line);
        examples->Add(ex);

        Learn(ex);
      }

      auto empty = GetOrCreateNativeExample();
      examples->Add(empty);
      empty->MakeEmpty(this);
      Learn(empty);

      return examples[0]->GetPrediction(this, predictionFactory);
    }
    finally
    { for each (auto ex in examples)
    {
      delete ex;
    }
    }
  }

  generic<typename T> T VowpalWabbit::Predict(IEnumerable<String^>^ lines, IVowpalWabbitPredictionFactory<T>^ predictionFactory)
  {
#if _DEBUG
    if (lines == nullptr)
      throw gcnew ArgumentNullException("lines");
#endif

    auto examples = gcnew List<VowpalWabbitExample^>;

    try
    {
      for each (auto line in lines)
      {
        auto ex = ParseLine(line);
        examples->Add(ex);

        Predict(ex);
      }

      auto empty = GetOrCreateNativeExample();
      examples->Add(empty);
      empty->MakeEmpty(this);
      Predict(empty);

      return examples[0]->GetPrediction(this, predictionFactory);
    }
    finally
    { for each (auto ex in examples)
    {
      delete ex;
    }
    }
  }

  void VowpalWabbit::EndOfPass()
  {
    try
    {
      m_vw->l->end_pass();
      sync_stats(*m_vw);
    }
    CATCHRETHROW
  }

  /// <summary>
  /// Hashes the given value <paramref name="s"/>.
  /// </summary>
  /// <param name="s">String to be hashed.</param>
  /// <param name="u">Hash offset.</param>
  /// <returns>The resulting hash code.</returns>
  //template<bool replaceSpace>
  size_t hashall(String^ s, size_t u)
  { // get raw bytes from string
    auto keys = Encoding::UTF8->GetBytes(s);
    int length = keys->Length;

    // TOOD: benchmark and verify correctness
    //if (replaceSpace)
    //{
    //  for (int j = 0; j < length;)
    //  {
    //    var k = keys[j];
    //    if (k == ' ')
    //    {
    //      keys[j] = '_';
    //    }

    //    j++;

    //    // take care of UTF-8 multi-byte characters
    //    while (k & 0xC == 0xC)
    //    {
    //      j++;
    //      k <<= 1;
    //    }
    //  }
    //}

    uint32_t h1 = u;
    uint32_t k1 = 0;

    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    int i = 0;
    while (i <= length - 4)
    { // convert byte array to integer
      k1 = (uint32_t)(keys[i] | keys[i + 1] << 8 | keys[i + 2] << 16 | keys[i + 3] << 24);

      k1 *= c1;
      k1 = ROTL32(k1, 15);
      k1 *= c2;

      h1 ^= k1;
      h1 = ROTL32(h1, 13);
      h1 = h1 * 5 + 0xe6546b64;

      i += 4;
    }

    k1 = 0;
    int tail = length - length % 4;
    switch (length & 3)
    {
    case 3:
      k1 ^= (uint32_t)(keys[tail + 2] << 16);
    case 2:
      k1 ^= (uint32_t)(keys[tail + 1] << 8);
    case 1:
      k1 ^= (uint32_t)(keys[tail]);
      k1 *= c1;
      k1 = ROTL32(k1, 15);
      k1 *= c2;
      h1 ^= k1;
      break;
    }

    // finalization
    h1 ^= (uint32_t)length;

    return MURMUR_HASH_3::fmix(h1);
  }

  /// <summary>
  /// Hashes the given value <paramref name="s"/>.
  /// </summary>
  /// <param name="s">String to be hashed.</param>
  /// <param name="u">Hash offset.</param>
  /// <returns>The resulting hash code.</returns>
  size_t hashstring(String^ s, size_t u)
  {
    s = s->Trim();

    int sInt = 0;
    if (int::TryParse(s, sInt))
    {
      return sInt + u;
    }
    else
    {
      return hashall(s, u);
    }
  }

  Func<String^, size_t, size_t>^ VowpalWabbit::GetHasher()
  {
    //feature manipulation
    string hash_function("strings");
    if (m_vw->vm.count("hash"))
    {
      hash_function = m_vw->vm["hash"].as<string>();
    }

    if (hash_function == "strings")
    {
      return gcnew Func<String^, size_t, size_t>(&hashstring);
    }
    else if (hash_function == "all")
    {
      return gcnew Func<String^, size_t, size_t>(&hashall);
    }
    else
    {
      THROW("Unsupported hash function: " << hash_function);
    }
  }

  VowpalWabbit^ VowpalWabbit::Native::get()
  {
    return this;
  }


  VowpalWabbitExample^ VowpalWabbit::GetOrCreateNativeExample()
  {
    if (m_examples->Count == 0)
    {
      try
      {
        auto ex = VW::alloc_examples(0, 1);
        m_vw->p->lp.default_label(&ex->l);
        return gcnew VowpalWabbitExample(this, ex);
      }
      CATCHRETHROW
    }

    auto ex = m_examples->Pop();

    try
    {
      VW::empty_example(*m_vw, *ex->m_example);
      m_vw->p->lp.default_label(&ex->m_example->l);

      return ex;
    }
    CATCHRETHROW
  }

  void VowpalWabbit::ReturnExampleToPool(VowpalWabbitExample^ ex)
  {
#if _DEBUG
    if (m_vw == nullptr)
      throw gcnew ObjectDisposedException("VowpalWabbitExample was not properly disposed as the owner is already disposed");

    if (ex == nullptr)
      throw gcnew ArgumentNullException("ex");
#endif

    // make sure we're not a ring based example
    assert(!VW::is_ring_example(*m_vw, ex->m_example));

    if (m_examples != nullptr)
      m_examples->Push(ex);
#if _DEBUG
    else // this should not happen as m_vw is already set to null
      throw gcnew ObjectDisposedException("VowpalWabbitExample was disposed after the owner is disposed");
#endif
  }
}
