#ifndef STOCK_DEBUG_QUERIES_HPP
#define STOCK_DEBUG_QUERIES_HPP

namespace wot
{

// Reset the ping field as part of the statistics owner reset.
void resetStockStatPing();

// #1513 006d8340 resets/enables, or stops while retaining the last FPS result.
void enableStockBattleStatistics( bool enabled );

// #1513 enableLoadingTimer writes +0x2642; the per-frame tick then keeps the ping.
void setStockLoadingTimer( bool enabled );

} // namespace wot

#endif // STOCK_DEBUG_QUERIES_HPP
