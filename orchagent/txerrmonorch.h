#ifndef TXERRMONORCH_ORCH_H
#define TXERRMONORCH_ORCH_H

#include "orch.h"
#include "port.h"
#include "portsorch.h"

#include "observer.h"

#include "timer.h"
#include "producerstatetable.h"
#include "selectabletimer.h"

#include <array>
#include <map>
#include <tuple>
#include <inttypes.h>

extern "C" {
#include "sai.h"
}

#define TXERRMONORCH_FIELD_CFG_PERIOD       "tx_err_mon_orch_polling_period"
#define TXERRMONORCH_FIELD_CFG_THRESHOLD    "tx_err_mon_orch_threshold"

#define TXERRMONORCH_FIELD_APPL_STATS      "txerrmon_stats"

#define TXERRMONORCH_FIELD_STATE_STATUS      "txerrmon_status"

#define TXERRMONORCH_SEL_TIMER     "TX_ERR_MON_COUNTERS_POLL"

#define TXERRMONORCH_KEY_CFG_POLLING_PERIOD "POLLING_PERIOD"

/*TX Err Monitor states definition*/
#define TXERRMONORCH_PORT_STATE_OK         0
#define TXERRMONORCH_PORT_STATE_ERROR      1
#define TXERRMONORCH_PORT_STATE_UNKNOWN    2

typedef std::tuple<int, sai_object_id_t, uint64_t, uint64_t> TxErrorMonStats;//state, stats, threshold
typedef std::map<std::string, TxErrorMonStats> TxErrorMonMap;

class TxErrMonOrch: public Orch
{
public:
    TxErrMonOrch(TableConnector stateDBc,
                 TableConnector confDBc,
                 TableConnector appDBc);

private:
    virtual ~TxErrMonOrch(void);
    uint16_t m_pollInterval;

    TxErrorMonMap m_PortsTxErrMonStats;

    Table m_TxErrorsTable;
    Table m_TxErrorStateTable;

    virtual void doTask(swss::SelectableTimer &timer);
    virtual void doTask(Consumer &consumer);
    virtual void txErrCounterCheck();
    virtual int txErrPollingPeriodUpdate(const vector<FieldValueTuple>& data);
    virtual int txErrThresholdUpdate(const string &port, const vector<FieldValueTuple>& data, bool clear);
};

#endif
