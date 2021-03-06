// Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "paddle/fluid/memory/allocation/best_fit_allocator.h"
#include <random>
#include <thread>  // NOLINT
#include <vector>
#include "gtest/gtest.h"
#include "paddle/fluid/memory/allocation/cpu_allocator.h"
#include "paddle/fluid/memory/allocation/locked_allocator.h"

namespace paddle {
namespace memory {
namespace allocation {

class StubAllocation : public Allocation {
 public:
  explicit StubAllocation(size_t size)
      : Allocation(0, size, platform::CPUPlace()) {}
};

TEST(BestFitAllocator, test_allocation) {
  StubAllocation stub(4UL * 1024 * 1024 * 1024);
  BestFitAllocator allocator(&stub);
  { auto allocation = allocator.Allocate(64, allocator.kDefault); }

  {
    auto allocation = allocator.Allocate(80, allocator.kDefault);

    {
      auto best_fit_allocation =
          dynamic_cast<BestFitAllocation*>(allocation.get());
      ASSERT_NE(best_fit_allocation, nullptr);
      ASSERT_FALSE(best_fit_allocation->ChunkIterator()->is_free);
      ASSERT_EQ(best_fit_allocation->ChunkIterator()->offset_, 0);
      ASSERT_EQ(allocation->size(), 80);
      ASSERT_EQ(allocation->ptr(), nullptr);
    }

    auto allocation2 = allocator.Allocate(60, allocator.kDefault);
    auto allocation3 = allocator.Allocate(90, allocator.kDefault);
    allocation2.reset();
    allocation2 = allocator.Allocate(30, allocator.kDefault);

    {
      auto best_fit_allocation =
          dynamic_cast<BestFitAllocation*>(allocation2.get());
      ASSERT_EQ(best_fit_allocation->ChunkIterator()->offset_, 80);
    }
    allocation2.reset();
    allocation2 = allocator.Allocate(60, allocator.kDefault);

    {
      auto best_fit_allocation =
          dynamic_cast<BestFitAllocation*>(allocation2.get());
      ASSERT_EQ(best_fit_allocation->ChunkIterator()->offset_, 80);
    }

    allocation.reset();
    allocation2.reset();

    allocation = allocator.Allocate(80 + 60, allocator.kDefault);
    {
      auto best_fit_allocation =
          dynamic_cast<BestFitAllocation*>(allocation.get());
      ASSERT_EQ(best_fit_allocation->ChunkIterator()->offset_, 0);
    }

    allocation.reset();

    allocation = allocator.Allocate(80, allocator.kDefault);
    allocation2 = allocator.Allocate(60, allocator.kDefault);
    allocation = nullptr;
    allocation2 = nullptr;
    allocation3 = nullptr;

    ASSERT_EQ(allocator.NumFreeChunks(), 1U);
  }
}

TEST(BestFitAllocator, test_concurrent_cpu_allocation) {
  CPUAllocator allocator;
  auto global_allocation =
      allocator.Allocate(256UL * 1024 * 1024, allocator.kDefault);

  std::unique_ptr<Allocator> best_fit_allocator(
      new BestFitAllocator(global_allocation.get()));

  LockedAllocator locked_allocator(std::move(best_fit_allocator));

  auto th_main = [&](std::random_device::result_type seed) {
    std::default_random_engine engine(seed);
    std::uniform_int_distribution<size_t> dist(1U, 1024U);

    for (size_t i = 0; i < 128; ++i) {
      size_t allocate_size = dist(engine);

      auto allocation = locked_allocator.Allocate(
          sizeof(size_t) * allocate_size, locked_allocator.kDefault);

      size_t* data = reinterpret_cast<size_t*>(allocation->ptr());

      for (size_t j = 0; j < allocate_size; ++j) {
        data[j] = j;
      }
      std::this_thread::yield();

      for (size_t j = 0; j < allocate_size; ++j) {
        ASSERT_EQ(data[j], j);
      }
    }
  };
  {
    std::vector<std::thread> threads;
    for (size_t i = 0; i < 1024; ++i) {
      std::random_device dev;
      threads.emplace_back(th_main, dev());
    }
    for (auto& th : threads) {
      th.join();
    }
  }
}

}  // namespace allocation
}  // namespace memory
}  // namespace paddle
