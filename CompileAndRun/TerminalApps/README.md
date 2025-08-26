These files contain the entry points for each demo terminal app: `Broker`, `ExchangeServer`, and `Exchange`. Each one essentially runs an infinite loop that waits for user input. 

The bash scripts in `CompileAndRun` actually compile and run these files. Below are brief summaries of what each one can do.

## Broker
- Enter "`AUTO`" to start sending automated, random $AAPL orders
- Hit `Enter` to send a singular random order
- Manual orders:
    - `BUY  <qty> <symbol>`           (send market order)
    - `SELL <qty> <symbol>  `         (send market order)
    - `BUY  <qty> <symbol> LMT <px>`  (send limit order)
    - `SELL <qty> <symbol> LMT <px>`  (send limit order)
    - `CANCEL <order_id>`             (cancel resting order)
- `Cntrl-C` to disconnect
- Enter `HELP` to view summary of all possible commands (like this page)

## ExchangeServer
- Hit `Enter` to toggle the open/closed status
- `Cntrl-C` to shutdown

## Exchange
- Hit `Enter` to view the current order book for $AAPL
- Enter a symbol to view that symbol's orderbook