/*
 * Copyright (C) 2015 Jared Boone, ShareBrained Technology, Inc.
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "proc_ert.hpp"

#include "portapack_shared_memory.hpp"

#include "i2s.hpp"
using namespace lpc43xx;

float ERTProcessor::abs(const complex8_t& v) {
	// const int16_t r = v.real() - offset_i;
	// const int16_t i = v.imag() - offset_q;
	// const uint32_t r2 = r * r;
	// const uint32_t i2 = i * i;
	// const uint32_t r2_i2 = r2 + i2;
	// return std::sqrt(static_cast<float>(r2_i2));
	const float r = static_cast<float>(v.real()) - offset_i;
	const float i = static_cast<float>(v.imag()) - offset_q;
	const float r2 = r * r;
	const float i2 = i * i;
	const float r2_i2 = r2 + i2;
	return std::sqrt(r2_i2);
}

void ERTProcessor::execute(buffer_c8_t buffer) {
	/* 4.194304MHz, 2048 samples */
	// auto decimator_out = decimator.execute(buffer);

	const complex8_t* src = &buffer.p[0];
	const complex8_t* const src_end = &buffer.p[buffer.count];

	average_i += src->real();
	average_q += src->imag();
	average_count++;
	if( average_count == 2048 ) {
		offset_i = static_cast<float>(average_i) / 2048.0f;
		offset_q = static_cast<float>(average_q) / 2048.0f;
		average_i = 0;
		average_q = 0;
		average_count = 0;
	}

	const float gain = 128 * samples_per_symbol;
	const float k = 1.0f / gain;

	while(src < src_end) {
		float sum = 0.0f;
		for(size_t i=0; i<(samples_per_symbol / 2); i++) {
			sum += abs(*(src++));
		}
		sum_half_period[1] = sum_half_period[0];
		sum_half_period[0] = sum;

		sum_period[2] = sum_period[1];
		sum_period[1] = sum_period[0];
		sum_period[0] = (sum_half_period[0] + sum_half_period[1]) * k;

		manchester[2] = manchester[1];
		manchester[1] = manchester[0];
		manchester[0] = sum_period[2] - sum_period[0];

		const auto data = manchester[0] - manchester[2];

		clock_recovery(data);
	}

	i2s::i2s0::tx_mute();
}

void ERTProcessor::consume_symbol(
	const float raw_symbol
) {
	const uint_fast8_t sliced_symbol = (raw_symbol >= 0.0f) ? 1 : 0;
	scm_builder.execute(sliced_symbol);
	idm_builder.execute(sliced_symbol);
}

void ERTProcessor::scm_handler(
	const std::bitset<1024>& payload,
	const size_t bits_received
) {
	ERTPacketMessage message;
	message.packet.preamble = 0x1f2a60;
	message.packet.payload = payload;
	message.packet.bits_received = bits_received;
	shared_memory.application_queue.push(message);
}

void ERTProcessor::idm_handler(
	const std::bitset<1024>& payload,
	const size_t bits_received
) {
	ERTPacketMessage message;
	message.packet.preamble = 0x555516a3;
	message.packet.payload = payload;
	message.packet.bits_received = bits_received;
	shared_memory.application_queue.push(message);
}