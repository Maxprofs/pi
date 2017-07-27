/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <graphene/chain/database.hpp>
#include <graphene/chain/protocol/incentive.hpp>
#include <fc/real128.hpp>

using namespace fc;

namespace graphene { namespace chain {
    signed_transaction database::generate_incentive_transaction() {
        signed_transaction tx;
        const auto& gpo = get_global_properties();
        auto generate_op = [&] (const construction_capital_object &cc_obj, uint8_t reason) {
            // at most GRAPHENE_DEFAULT_MAX_INCENTIVE_OPERATIONS_PER_BLOCK incentive ops per block
            if (tx.operations.size() >= gpo.parameters.max_incentive_operations_per_block) {
                return;
            }
            // pay back save money by period
            real128 amount0 = real128(cc_obj.amount.value) / real128(cc_obj.total_periods);
            // pay interest
            real128 amount1 = real128(cc_obj.amount.value)
                * real128(cc_obj.period) / real128(GRAPHENE_SECONDS_PER_YEAR) 
                * real128(gpo.parameters.issuance_rate) / real128(GRAPHENE_ISSUANCE_RATE_SCALE);
            real128 amount = amount0 + amount1;
            // generate incentive operation
            incentive_operation op;
            op.amount = amount.to_uint64();
            op.ccid = cc_obj.id;
            op.reason = reason;
            tx.operations.push_back(op);
            wlog("incentive: ${incentive}", ("incentive", op));
        };
        //add incentive generated by construction capital period
        const auto& index = get_index_type<construction_capital_index>().indices().get<by_next_slot>();
        auto upper_it = index.upper_bound(head_block_time());
        for (auto it = index.begin(); it != upper_it; ++it) {
            generate_op(*it, 0);
        }
        //add incentive generated by construction capital vote
        const auto& index_pending = get_index_type<construction_capital_index>().indices().get<by_pending>();
        for (auto it = index_pending.rbegin(); it != index_pending.rend() && it->pending > 0; ++it) {
            for (uint16_t i = 0; i < it->pending; i++) {
                generate_op(*it, 1);
            }
        }
        //set tx params
        auto dyn_props = get_dynamic_global_properties();
        tx.set_reference_block(dyn_props.head_block_id);
        tx.set_expiration( dyn_props.time + fc::seconds(30) );

        return tx;
    }
    
    processed_transaction database::apply_incentive(const processed_transaction &tx) {
        transaction_evaluation_state eval_state(this);
        //process the operations
        processed_transaction ptrx(tx);
        _current_op_in_trx = 0;
        for( const auto& op : ptrx.operations )
        {
            eval_state.operation_results.emplace_back(apply_operation(eval_state, op));
            ++_current_op_in_trx;
        }
        ptrx.operation_results = std::move(eval_state.operation_results);

        return ptrx;
    }
}}
