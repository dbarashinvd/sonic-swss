#include "txerrmonorch.h"

#include "converter.h"

extern sai_port_api_t *sai_port_api;

extern PortsOrch *gPortsOrch;

string tx_state_status_names [] = {"PORT_STATE_OK", "PORT_STATE_ERROR", "PORT_STATE_UNKNOWN"};


TxErrMonOrch::TxErrMonOrch(TableConnector stateDBc,
                           TableConnector confDBc,
                           TableConnector appDBc):
    Orch(confDBc.first, confDBc.second),
    m_TxErrorsTable(appDBc.first, appDBc.second),
    m_TxErrorStateTable(stateDBc.first, stateDBc.second),
    m_PortsTxErrMonStats(),
    m_pollInterval(10)
{
    SWSS_LOG_ENTER();

    auto timespec_interval = timespec { .tv_sec = m_pollInterval, .tv_nsec = 0 };
    auto timer = new SelectableTimer(timespec_interval);
    auto executor = new ExecutableTimer(timer, this, TXERRMONORCH_SEL_TIMER );
    Orch::addExecutor(executor);
    timer->start();
}

TxErrMonOrch::~TxErrMonOrch(void)
{
    SWSS_LOG_ENTER();
}

void TxErrMonOrch::txErrCounterCheck()
{

    if (!gPortsOrch->allPortsReady())
    {
        SWSS_LOG_NOTICE("TX_ERR_MON: not all ports ready yet");
        return;
    }

    SWSS_LOG_ENTER();
    vector<FieldValueTuple> StateFieldValues;
    vector<FieldValueTuple> AppFieldValues;
    static const vector<sai_stat_id_t> txOversizePktsStatId = {SAI_PORT_STAT_IF_OUT_ERRORS/*SAI_PORT_STAT_ETHER_RX_OVERSIZE_PKTS*/};
    uint64_t tx_oversize_pkts = 0, curr_stats = 0, prev_stats = 0;
    sai_status_t sai_status;
    for (auto &i : m_PortsTxErrMonStats)
    {
        Port port;
        if (!gPortsOrch->getPort(i.first.c_str(), port))
        {
            SWSS_LOG_ERROR("Invalid port alias %s", i.first.c_str());
            AppFieldValues.emplace_back(TXERRMONORCH_FIELD_APPL_STATS, to_string(-2));
        }
        else
        {
            sai_status = sai_port_api->get_port_stats(port.m_port_id,
                                         static_cast<uint32_t>(txOversizePktsStatId.size()),
                                         txOversizePktsStatId.data(),
                                         &tx_oversize_pkts);

            if (SAI_STATUS_SUCCESS != sai_status)
            {
                SWSS_LOG_ERROR("TX_ERR_MON failed to get port stats with sai_port_api->get_port_stats "
                        "for port %s\n", i.first.c_str());

            }
            else
            {
                SWSS_LOG_NOTICE("TX_ERR_MON saving tx_oversize_pkts from sai api: %lu for port %s into curr_stat\n"
                        , tx_oversize_pkts, i.first.c_str());
                curr_stats = tx_oversize_pkts;

                // set state according to values and threshold
                auto threshold = std::get<3>(i.second);
                auto port_state = TXERRMONORCH_PORT_STATE_UNKNOWN;
                prev_stats = std::get<2>(i.second);
                if (curr_stats - prev_stats > threshold)
                {
                    port_state = TXERRMONORCH_PORT_STATE_ERROR;
                    SWSS_LOG_NOTICE("TX_ERR_MON port_state set to TXERRMONORCH_PORT_STATE_ERROR %d for port %s\n"
                            , TXERRMONORCH_PORT_STATE_ERROR, i.first.c_str());
                }
                else
                {
                    port_state = TXERRMONORCH_PORT_STATE_OK;
                    SWSS_LOG_NOTICE("TX_ERR_MON port_state set to TXERRMONORCH_PORT_STATE_OK %d for port %s\n"
                            , TXERRMONORCH_PORT_STATE_OK, i.first.c_str());
                }

                // save current err state into local data structure and also to state DB
                std::get<0>(i.second) = port_state;
                StateFieldValues.emplace_back(TXERRMONORCH_FIELD_STATE_STATUS, tx_state_status_names[port_state]);

                // save current and previous err counter into local data structure and App DB
                SWSS_LOG_NOTICE("TX_ERR_MON port stats before update: curr stats %lu prev stats %lu "
                        "for port %s\n", curr_stats, prev_stats, i.first.c_str());
                std::get<2>(i.second) = curr_stats;
                AppFieldValues.emplace_back(TXERRMONORCH_FIELD_APPL_STATS, to_string(std::get<2>(i.second)));
            }
        }
        m_TxErrorStateTable.set(i.first.c_str(), StateFieldValues);
        m_TxErrorsTable.set(i.first.c_str(), AppFieldValues);
    }
    m_TxErrorStateTable.flush();
    m_TxErrorsTable.flush();
}

void TxErrMonOrch::doTask(swss::SelectableTimer &timer)
{
    SWSS_LOG_ENTER();
    txErrCounterCheck();
}

int TxErrMonOrch::txErrPollingPeriodUpdate(const vector<FieldValueTuple>& data)
{
    SWSS_LOG_ENTER();

    try
    {
        for (auto i : data)
        {
            if (TXERRMONORCH_FIELD_CFG_PERIOD == fvField(i))
            {
                m_pollInterval = to_uint<uint16_t>(fvValue(i));
                SWSS_LOG_NOTICE("TxErrMon polling period update to %d\n", m_pollInterval);
            }
            else
            {
                SWSS_LOG_ERROR("Unknown field type %s when handle polling period update\n",
                               fvField(i).c_str());
                return -1;
            }
        }
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to set polling period\n");
    }

    return 0;
}

int TxErrMonOrch::txErrThresholdUpdate(const string &port, const vector<FieldValueTuple> &data, bool del)
{
    SWSS_LOG_ENTER();

    try
    {
        if (del)
        {
            m_PortsTxErrMonStats.erase(port);
            m_TxErrorsTable.del(port);
            m_TxErrorStateTable.del(port);
            SWSS_LOG_NOTICE("TX_ERR_MON threshold cleared for port %s\n", port.c_str());
        }
        else
        {
            for (auto i : data)
            {
                if (TXERRMONORCH_FIELD_CFG_THRESHOLD == fvField(i))
                {
                    TxErrorMonStats &TxErrMonVect = m_PortsTxErrMonStats[port];
                    //the first time this port is configured
                    if (std::get<1>(TxErrMonVect) == 0)
                    {
                        Port saiport;
                        std::get<0>(TxErrMonVect) = TXERRMONORCH_PORT_STATE_UNKNOWN;
                    }
                    std::get<3>(TxErrMonVect) = to_uint<uint64_t>(fvValue(i));
                    SWSS_LOG_ERROR("TxErrMon set threshold to %ld for port %s\n",
                                  std::get<3>(TxErrMonVect), port.c_str());
                }
                else
                {
                    SWSS_LOG_ERROR("Unknown field type %s when handle threshold for %s\n",
                                   fvField(i).c_str(), port.c_str());
                    return -1;
                }
            }
        }
    }
    catch (...)
    {
        SWSS_LOG_ERROR("Failed to set threshold for port %s\n", port.c_str());
    }

    return 0;
}

void TxErrMonOrch::doTask(Consumer& consumer)
{
    int rc = 0;

    SWSS_LOG_ENTER();

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        KeyOpFieldsValuesTuple kofvt = it->second;

        string key = kfvKey(kofvt);
        string op = kfvOp(kofvt);
        vector<FieldValueTuple> fvs = kfvFieldsValues(kofvt);

        rc = -1;

        SWSS_LOG_INFO("TX_ERR_MON %s operation %s set %s del %s\n",
                      key.c_str(),
                      op.c_str(), SET_COMMAND, DEL_COMMAND);
        if (key == TXERRMONORCH_KEY_CFG_POLLING_PERIOD)
        {
            if (op == SET_COMMAND)
            {
                rc = txErrPollingPeriodUpdate(fvs);
            }
            else
            {
                SWSS_LOG_ERROR("Unknown operation type %s when set period\n", op.c_str());
            }
        }
        else //key should be the alias of interface
        {
            if (op == SET_COMMAND)
            {
                // fetch the value which reprsents threshold
                rc = txErrThresholdUpdate(key, fvs, false);
            }
            else if (op == DEL_COMMAND)
            {
                // delete interface from table
                rc = txErrThresholdUpdate(key, fvs, true);
            }
            else
            {
                SWSS_LOG_ERROR("Unknown operation type %s when set threshold\n", op.c_str());
            }
        }

        if (rc)
        {
            SWSS_LOG_ERROR("configuration update failed with key %s\n", key.c_str());
        }

        consumer.m_toSync.erase(it++);
    }
}
