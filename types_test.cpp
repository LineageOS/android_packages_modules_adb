/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "types.h"

#include <gtest/gtest.h>

#include <memory>
#include <type_traits>
#include <utility>

#include "adb.h"
#include "apacket_reader.h"
#include "fdevent/fdevent_test.h"

static IOVector::block_type create_block(const std::string& string) {
    return IOVector::block_type(string.begin(), string.end());
}

static IOVector::block_type create_block(char value, size_t len) {
    auto block = IOVector::block_type();
    block.resize(len);

    static_assert(std::is_standard_layout<decltype(block)>());
    memset(&(block)[0], value, len);

    return block;
}

template <typename T>
static IOVector::block_type copy_block(const T& block) {
    auto copy = IOVector::block_type();
    copy.assign(block.begin(), block.end());
    return copy;
}

TEST(IOVector, empty) {
    // Empty IOVector.
    IOVector bc;
    CHECK_EQ(0ULL, bc.coalesce().size());
}

TEST(IOVector, move_constructor) {
    IOVector x;
    size_t xsize = x.coalesce().size();
    IOVector y(std::move(x));
    CHECK_EQ(xsize, y.coalesce().size());
}

TEST(IOVector, single_block) {
    // A single block.
    auto block = create_block('x', 100);
    IOVector bc;
    bc.append(copy_block(block));
    ASSERT_EQ(100ULL, bc.size());
    auto coalesced = bc.coalesce();
    ASSERT_EQ(block, coalesced);
}

TEST(IOVector, single_block_split) {
    // One block split.
    IOVector bc;
    bc.append(create_block("foobar"));
    IOVector foo = bc.take_front(3);
    ASSERT_EQ(3ULL, foo.size());
    ASSERT_EQ(3ULL, bc.size());
    ASSERT_EQ(create_block("foo"), foo.coalesce());
    ASSERT_EQ(create_block("bar"), bc.coalesce());
}

TEST(IOVector, aligned_split) {
    IOVector bc;
    bc.append(create_block("foo"));
    bc.append(create_block("bar"));
    bc.append(create_block("baz"));
    ASSERT_EQ(9ULL, bc.size());

    IOVector foo = bc.take_front(3);
    ASSERT_EQ(3ULL, foo.size());
    ASSERT_EQ(create_block("foo"), foo.coalesce());

    IOVector bar = bc.take_front(3);
    ASSERT_EQ(3ULL, bar.size());
    ASSERT_EQ(create_block("bar"), bar.coalesce());

    IOVector baz = bc.take_front(3);
    ASSERT_EQ(3ULL, baz.size());
    ASSERT_EQ(create_block("baz"), baz.coalesce());

    ASSERT_EQ(0ULL, bc.size());
}

TEST(IOVector, misaligned_split) {
    IOVector bc;
    bc.append(create_block("foo"));
    bc.append(create_block("bar"));
    bc.append(create_block("baz"));
    bc.append(create_block("qux"));
    bc.append(create_block("quux"));

    // Aligned left, misaligned right, across multiple blocks.
    IOVector foob = bc.take_front(4);
    ASSERT_EQ(4ULL, foob.size());
    ASSERT_EQ(create_block("foob"), foob.coalesce());

    // Misaligned left, misaligned right, in one block.
    IOVector a = bc.take_front(1);
    ASSERT_EQ(1ULL, a.size());
    ASSERT_EQ(create_block("a"), a.coalesce());

    // Misaligned left, misaligned right, across two blocks.
    IOVector rba = bc.take_front(3);
    ASSERT_EQ(3ULL, rba.size());
    ASSERT_EQ(create_block("rba"), rba.coalesce());

    // Misaligned left, misaligned right, across three blocks.
    IOVector zquxquu = bc.take_front(7);
    ASSERT_EQ(7ULL, zquxquu.size());
    ASSERT_EQ(create_block("zquxquu"), zquxquu.coalesce());

    ASSERT_EQ(1ULL, bc.size());
    ASSERT_EQ(create_block("x"), bc.coalesce());
}

TEST(IOVector, drop_front) {
    IOVector vec;

    vec.append(create_block('x', 2));
    vec.append(create_block('y', 1000));
    ASSERT_EQ(2U, vec.front_size());
    ASSERT_EQ(1002U, vec.size());

    vec.drop_front(1);
    ASSERT_EQ(1U, vec.front_size());
    ASSERT_EQ(1001U, vec.size());

    vec.drop_front(1);
    ASSERT_EQ(1000U, vec.front_size());
    ASSERT_EQ(1000U, vec.size());
}

TEST(IOVector, take_front) {
    IOVector vec;
    ASSERT_TRUE(vec.take_front(0).empty());

    vec.append(create_block('x', 2));
    ASSERT_EQ(2ULL, vec.size());

    ASSERT_EQ(1ULL, vec.take_front(1).size());
    ASSERT_EQ(1ULL, vec.size());

    ASSERT_EQ(1ULL, vec.take_front(1).size());
    ASSERT_EQ(0ULL, vec.size());
}

TEST(IOVector, trim_front) {
    IOVector vec;
    vec.append(create_block('x', 2));

    ASSERT_EQ(1ULL, vec.take_front(1).size());
    ASSERT_EQ(1ULL, vec.size());
    vec.trim_front();
    ASSERT_EQ(1ULL, vec.size());
}

class weak_ptr_test : public FdeventTest {};

struct Destructor : public enable_weak_from_this<Destructor> {
    Destructor(bool* destroyed) : destroyed_(destroyed) {}
    ~Destructor() { *destroyed_ = true; }

    bool* destroyed_;
};

TEST_F(weak_ptr_test, smoke) {
    PrepareThread();

    Destructor* destructor = nullptr;
    bool destroyed = false;
    std::optional<weak_ptr<Destructor>> p;

    fdevent_run_on_looper([&p, &destructor, &destroyed]() {
        destructor = new Destructor(&destroyed);
        p = destructor->weak();
        ASSERT_TRUE(p->get());

        p->reset();
        ASSERT_FALSE(p->get());

        p->reset(destructor);
        ASSERT_TRUE(p->get());
    });
    WaitForFdeventLoop();
    ASSERT_TRUE(destructor);
    ASSERT_FALSE(destroyed);

    destructor->schedule_deletion();
    WaitForFdeventLoop();

    ASSERT_TRUE(destroyed);
    fdevent_run_on_looper([&p]() {
        ASSERT_FALSE(p->get());
        p.reset();
    });

    TerminateThread();
}

void ASSERT_APACKET_EQ(const apacket& expected, const std::unique_ptr<apacket>& result) {
    ASSERT_EQ(expected.msg.data_length, result->msg.data_length);
    ASSERT_EQ(expected.msg.command, result->msg.command);
    ASSERT_EQ(expected.msg.arg0, result->msg.arg0);
    ASSERT_EQ(expected.msg.arg1, result->msg.arg1);
    ASSERT_EQ(expected.msg.data_check, result->msg.data_check);
    ASSERT_EQ(expected.msg.magic, result->msg.magic);
    ASSERT_EQ(size_t(0), expected.payload.position());
    ASSERT_EQ(size_t(0), result->payload.position());

    ASSERT_EQ(expected.payload.remaining(), result->payload.remaining());
    ASSERT_EQ(0, memcmp(expected.payload.data(), result->payload.data(),
                        expected.payload.remaining()));
}

void ASSERT_APACKETS_EQ(const std::vector<apacket>& expected,
                        const std::vector<std::unique_ptr<apacket>>& result) {
    ASSERT_EQ(expected.size(), result.size());
    for (size_t i = 0; i < expected.size(); i++) {
        ASSERT_APACKET_EQ(expected[i], result[i]);
    }
}
static Block block_from_header(amessage& header) {
    Block b{sizeof(amessage)};
    memcpy(b.data(), reinterpret_cast<char*>(&header), sizeof(amessage));
    return b;
}

static apacket make_packet(uint32_t cmd, const std::string payload = "") {
    apacket p;
    p.msg.command = cmd;
    p.msg.data_length = payload.size();
    p.payload.resize(payload.size());
    memcpy(p.payload.data(), payload.data(), payload.size());
    return p;
}

static std::vector<Block> packets_to_blocks(const std::vector<apacket>& packets) {
    std::vector<Block> blocks;
    for (auto& p : packets) {
        // Create the header
        Block header{sizeof(amessage)};
        memcpy(header.data(), reinterpret_cast<const char*>(&p.msg), sizeof(amessage));
        blocks.emplace_back(std::move(header));

        // Create the payload
        if (p.msg.data_length != 0) {
            Block payload{p.msg.data_length};
            memcpy(payload.data(), p.payload.data(), p.msg.data_length);
            blocks.push_back(std::move(payload));
        }
    }
    return blocks;
}

TEST(APacketReader, initial_state) {
    APacketReader reader;
    auto packets = reader.get_packets();
    ASSERT_EQ(packets.size(), (size_t)0);
}

void runAndVerifyAPacketTest(std::vector<Block>& traffic, const std::vector<apacket>& expected) {
    adb_trace_enable(USB);
    // Feed the blocks to the reader (on the receiver end)
    APacketReader reader;
    for (auto& b : traffic) {
        auto res = reader.add_bytes(std::move(b));
        ASSERT_EQ(res, APacketReader::AddResult::OK);
    }

    // Make sure the input and the output match
    ASSERT_APACKETS_EQ(expected, reader.get_packets());
}

TEST(APacketReader, one_packet_two_blocks) {
    std::vector<apacket> input;
    input.emplace_back(make_packet(A_OKAY, "12345"));

    auto blocks = packets_to_blocks(input);
    ASSERT_EQ(size_t(2), blocks.size());

    runAndVerifyAPacketTest(blocks, input);
}

TEST(APacketReader, one_packet_empty_blocks) {
    std::vector<apacket> input;
    input.emplace_back(make_packet(A_OKAY, "12345"));

    auto blocks = packets_to_blocks(input);
    blocks.emplace(blocks.begin(), Block{0});
    blocks.emplace_back(Block{0});
    ASSERT_EQ(size_t(4), blocks.size());

    runAndVerifyAPacketTest(blocks, input);
}

TEST(APacketReader, no_payload) {
    std::vector<apacket> input;
    input.emplace_back(make_packet(A_OKAY));

    auto blocks = packets_to_blocks(input);
    // Make sure we have a single block with the header in it.
    ASSERT_EQ(size_t(1), blocks.size());
    ASSERT_EQ(sizeof(amessage), blocks[0].size());

    runAndVerifyAPacketTest(blocks, input);
}

TEST(APacketReader, several_no_payload) {
    std::vector<apacket> input;
    input.emplace_back(make_packet(A_OKAY));
    input.emplace_back(make_packet(A_WRTE));
    input.emplace_back(make_packet(A_CLSE));
    input.emplace_back(make_packet(A_CNXN));

    auto blocks = packets_to_blocks(input);
    // Make sure we have a single block with the header in it.
    ASSERT_EQ(size_t(4), blocks.size());
    for (const auto& block : blocks) {
        ASSERT_EQ(sizeof(amessage), block.size());
    }

    runAndVerifyAPacketTest(blocks, input);
}

TEST(APacketReader, payload_overflow) {
    std::vector<apacket> input;
    std::string payload = "0";
    input.emplace_back(make_packet(A_OKAY, payload));

    // Create a header block but a payload block with too much payload
    std::vector<Block> blocks;
    blocks.emplace_back(block_from_header(input[0].msg));
    blocks.emplace_back(payload + "0");

    runAndVerifyAPacketTest(blocks, input);
}

TEST(APacketReader, several_packets) {
    std::vector<apacket> input;
    for (int i = 0; i < 10; i++) {
        input.emplace_back(make_packet(i, std::string(i, (char)i)));
    }

    auto blocks = packets_to_blocks(input);
    ASSERT_EQ(size_t(19), blocks.size());  // Not 20, because first one has no payload!

    runAndVerifyAPacketTest(blocks, input);
}

TEST(APacketReader, split_header) {
    std::string payload = "0123456789";
    std::vector<apacket> input;
    input.emplace_back(make_packet(A_OKAY, payload));

    // We do some surgery here to split the header into two Blocks
    std::vector<Block> blocks;
    // First half of header
    Block header1(sizeof(amessage) / 2);
    memcpy(header1.data(), (char*)&input[0].msg, sizeof(amessage) / 2);
    blocks.emplace_back(std::move(header1));

    // Second half of header
    Block header2(sizeof(amessage) / 2);
    memcpy(header2.data(), ((char*)&input[0].msg) + sizeof(amessage) / 2, sizeof(amessage) / 2);
    blocks.emplace_back(std::move(header2));

    // Payload is not split
    blocks.emplace_back(Block{payload});

    runAndVerifyAPacketTest(blocks, input);
}

TEST(APacketReader, payload_and_next_header_merged) {
    std::vector<apacket> input;
    input.emplace_back(make_packet(A_OKAY, "12345"));
    std::string second_payload = "67890";
    input.emplace_back(make_packet(A_CLSE, second_payload));

    // We do some surgery here to merge the payload of first packet with header of second packet
    std::vector<Block> blocks;
    blocks.emplace_back(block_from_header(input[0].msg));
    Block mergedBlock{input[0].payload.size() + sizeof(amessage)};
    memcpy(mergedBlock.data(), input[0].payload.data(), input[0].msg.data_length);
    memcpy(mergedBlock.data() + input[0].msg.data_length, block_from_header(input[1].msg).data(),
           sizeof(amessage));
    blocks.emplace_back(std::move(mergedBlock));
    blocks.emplace_back(Block{second_payload});

    ASSERT_EQ(size_t(3), blocks.size());
    runAndVerifyAPacketTest(blocks, input);
}

static Block mergeBlocks(std::vector<Block>& blocks) {
    size_t total_size = 0;
    for (Block& b : blocks) {
        total_size += b.size();
    }
    Block block{total_size};
    size_t rover = 0;
    for (Block& b : blocks) {
        memcpy(block.data() + rover, b.data(), b.size());
        rover += b.size();
    }
    return block;
}

TEST(APacketReader, one_packet_one_block) {
    std::vector<apacket> input;
    input.emplace_back(make_packet(A_OKAY, "12345"));

    std::vector<Block> blocks_clean = packets_to_blocks(input);
    std::vector<Block> blocks;
    blocks.emplace_back(mergeBlocks(blocks_clean));

    runAndVerifyAPacketTest(blocks, input);
}

TEST(APacketReader, two_packets_one_block) {
    std::vector<apacket> input;
    input.emplace_back(make_packet(A_OKAY, "12345"));
    input.emplace_back(make_packet(A_WRTE, "67890"));

    std::vector<Block> blocks_clean = packets_to_blocks(input);
    std::vector<Block> blocks;
    blocks.emplace_back(mergeBlocks(blocks_clean));
    ASSERT_EQ(size_t(1), blocks.size());

    runAndVerifyAPacketTest(blocks, input);
}

TEST(APacketReader, bad_big_payload_header) {
    std::vector<apacket> input;
    input.emplace_back(make_packet(A_OKAY, std::string(MAX_PAYLOAD + 1, 'a')));
    std::vector<Block> blocks = packets_to_blocks(input);

    APacketReader reader;
    auto res = reader.add_bytes(std::move(blocks[0]));
    ASSERT_EQ(res, APacketReader::AddResult::ERROR);
}

std::vector<Block> splitBlock(Block src_block, size_t chop_size) {
    std::vector<Block> blocks;
    while (src_block.remaining()) {
        Block block{std::min(chop_size, src_block.remaining())};
        block.fillFrom(src_block);
        block.rewind();
        blocks.emplace_back(std::move(block));
    }
    return blocks;
}

// Collapse all packets into a single block. Chop it into chop_size Blocks.
// And feed that to the packet reader.
void chainSaw(int chop_size) {
    std::vector<apacket> packets;
    packets.emplace_back(make_packet(A_CNXN));
    packets.emplace_back(make_packet(A_OKAY, "12345"));
    packets.emplace_back(make_packet(A_WRTE, "6890"));
    packets.emplace_back(make_packet(A_CNXN));
    packets.emplace_back(make_packet(A_AUTH, "abc"));
    packets.emplace_back(make_packet(A_WRTE));
    ASSERT_EQ(size_t(6), packets.size());

    auto all_blocks = packets_to_blocks(packets);
    ASSERT_EQ(size_t(9), all_blocks.size());

    auto single_block = mergeBlocks(all_blocks);
    auto single_block_size = single_block.remaining();
    auto blocks = splitBlock(std::move(single_block), chop_size);
    auto expected_num_blocks =
            single_block_size / chop_size + (single_block_size % chop_size == 0 ? 0 : 1);
    ASSERT_EQ(expected_num_blocks, blocks.size());

    runAndVerifyAPacketTest(blocks, packets);
}

TEST(APacketReader, chainsaw) {
    // Try to send packets, chopping in various pieces sizes
    for (int i = 1; i < 256; i++) {
        chainSaw(i);
    }
}