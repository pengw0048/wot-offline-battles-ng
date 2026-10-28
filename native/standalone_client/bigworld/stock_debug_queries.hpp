#ifndef STOCK_DEBUG_QUERIES_HPP
#define STOCK_DEBUG_QUERIES_HPP

namespace wot
{

// #1513 enableBattleStatisticCollector(True) resets the statistics owner (006da280).
void resetStockStatPing();

// #1513 enableLoadingTimer writes +0x2642; the per-frame tick then keeps the ping.
void setStockLoadingTimer( bool enabled );

} // namespace wot

#endif // STOCK_DEBUG_QUERIES_HPP
