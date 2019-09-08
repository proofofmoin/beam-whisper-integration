// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "wallet_network.h"
#include <sstream>
#include <string>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <string>
#include <fstream>
#include <ios>

using namespace std;

static int hex2int(
    char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    
    if (c >= 'a' && c <= 'f')
        return c - 'a';
    
     if (c >= 'A' && c <= 'F')
        return c - 'A';
    
    return -1;
}

namespace
{
    const char* BBS_TIMESTAMPS = "BbsTimestamps";
    const unsigned AddressUpdateInterval_ms = 60 * 1000; // check addresses every minute

    beam::BbsChannel channel_from_wallet_id(const beam::wallet::WalletID& walletID)
    {
        beam::BbsChannel ret;
        walletID.m_Channel.Export(ret);
        return ret;
    }
}


namespace beam::wallet {

    ///////////////////////////



    BaseMessageEndpoint::BaseMessageEndpoint(IWallet& w, const IWalletDB::Ptr& pWalletDB)
        : m_Wallet(w)
        , m_WalletDB(pWalletDB)
        , m_AddressExpirationTimer(io::Timer::create(io::Reactor::get_Current()))
    {
        auto mc = io::Timer::create(io::Reactor::get_Current());
        mc->start(5000, true, [this] { CheckFileForMsgs(); });
    }

    BaseMessageEndpoint::~BaseMessageEndpoint()
    {
        
    }

    void BaseMessageEndpoint::Subscribe()
    {

        //DiP

        /*
        std::string line;
        std::vector<uint8_t> msg;
        std::getline(std::cin, line);
        if (line.find("ReceiveMsg") != std::string::npos ||
            line.find("SendMsg") != std::string::npos) {
            size_t tail = line.rfind("0x");
            tail += 2;
        
            for (size_t i = tail; i < line.size() -1; i+=2) {
                std::string data = line.substr(i, 2);
                msg.push_back(std::stoul(data, nullptr, 16));
            }

        }

        */

        auto myAddresses = m_WalletDB->getAddresses(true);
        for (const auto& address : myAddresses)
            if (!address.isExpired())
                AddOwnAddress(address);

        m_AddressExpirationTimer->start(AddressUpdateInterval_ms, false, [this] { OnAddressTimer(); });
    }

    void BaseMessageEndpoint::Unsubscribe()
    {
        while (!m_Addresses.empty())
            DeleteAddr(m_Addresses.begin()->get_ParentObj());
    }

    //DiP: 
    bool BaseMessageEndpoint::CheckFileForMsgs() {
        //fprintf(stderr, "=======> CHECK mylog.txtx \n");
        std::string filename("mylog.txt");
        static size_t last = 0; // start from the position 0
        
        ifstream infile; 
        infile.open(filename); 
        if (!infile.is_open())
            return false;

        //fprintf(stderr, "=======> OPENED mylog.txtx \n");


        infile.seekg(0,ios_base::end);
        size_t curr = infile.tellg();
        
        if (curr <= last) {
            infile.close();
            return false;
        }

        fprintf(stderr, "=======> HAS NEW LINES mylog.txtx \n");

/*
        // check for EOL, if not there message is incomplete
        infile.seekg(-1, std::ios_base::end);
        char c = '\0';
        infile.get(c);
        if (c != '\n' || c != '\r') {
            infile.close();
            return false;
        }
*/
        fprintf(stderr, "=======> FILE CORRECT mylog.txtx \n");


        // Read bytes
        infile.seekg(last, std::ios_base::beg);
        std::vector<char> buffer;
        buffer.reserve(curr-last + 1);
        std::copy( std::istreambuf_iterator<char>(infile),
           std::istreambuf_iterator<char>(),
           std::back_inserter(buffer) );
        buffer[buffer.size() -1] = '\0';   
        std::string full(buffer.data());   // to C++ string  
        std::string sep("\n");

        // split string by new lines
        while (full.length() > 0) {
            string line;
            size_t p = full.find(sep);
            if (p == std::string::npos) {
                line = full;
                full = "";
            } else {
                line = full.substr(0, p);
                full = full.substr(p+1);
            }
            
            fprintf(stderr, "=======> LINE %s \n",line.c_str());

            size_t x1 = line.rfind("0x");
            if (x1 == std::string::npos)
                continue;



            line = line.substr(x1 + 2);
            
            fprintf(stderr, "=======> LINE TRUNC %s \n",line.c_str());

            // convert line to bytes
            ByteBuffer msg;
            for (size_t i = 0; i < line.length(); i+=2) { 
                msg.push_back(uint8_t(16 * hex2int(line[i]) + hex2int(line[i+1])));
            }    
            BaseMessageEndpoint::CheckInMsg(msg);   
        }
        
        infile.close();
        last = curr;
        return true;
    }
     
   
    // check file for new messages
    bool BaseMessageEndpoint::CheckInMsg(const ByteBuffer& msg)  {
        //for (ChannelSet::iterator it : m_Channels)
        for (auto it : m_Channels) {
            ByteBuffer buf = msg; // duplicate
            uint8_t* pMsg = &buf.front();
            uint32_t nSize = static_cast<uint32_t>(buf.size());

            if (!proto::Bbs::Decrypt(pMsg, nSize, it.get_ParentObj().m_sk))
                continue;

            SetTxParameter msgWallet;
            bool bValid = false;

            try {
                Deserializer der;
                der.reset(pMsg, nSize);
                der& msgWallet;
                bValid = true;
            }
            catch (const std::exception&) {
                LOG_WARNING() << "BBS deserialization failed";
            }

            if (bValid)
            {
                WalletID wid;
                wid.m_Pk = it.get_ParentObj().m_Pk;
                wid.m_Channel = it.m_Value;
                m_Wallet.OnWalletMessage(wid, std::move(msgWallet));
                return true;
            }
        }
        return false;
    }
    //DiP: End

    void BaseMessageEndpoint::ProcessMessage(BbsChannel channel, const ByteBuffer& msg)
    {
        CheckFileForMsgs();
        Addr::Channel key;
        key.m_Value = channel;

        for (ChannelSet::iterator it = m_Channels.lower_bound(key); ; it++)
        {
            if (m_Channels.end() == it)
                break;
            if (it->m_Value != channel)
                break; // as well


            if (!m_WalletDB->get_MasterKdf())
            {
                // public wallet
                m_WalletDB->saveIncomingWalletMessage(channel, msg);
                OnIncomingMessage();
                return;
            }

            ByteBuffer buf = msg; // duplicate
//DiP:

        std::stringstream stream;
        stream << "0x" << std::setfill ('0') << std::setw(2) << std::hex;

        for (auto b : msg) {
            stream << std::setw(2) << int(b);
        }

        std::string str = stream.str(); // as string

        //fprintf(stderr, "RecieveMsg: %s\n\n", str.c_str());




            uint8_t* pMsg = &buf.front();
            uint32_t nSize = static_cast<uint32_t>(buf.size());

            if (!proto::Bbs::Decrypt(pMsg, nSize, it->get_ParentObj().m_sk))
                continue;

            SetTxParameter msgWallet;
            bool bValid = false;

            try {
                Deserializer der;
                der.reset(pMsg, nSize);
                der& msgWallet;
                bValid = true;
            }
            catch (const std::exception&) {
                LOG_WARNING() << "BBS deserialization failed";
            }

            if (bValid)
            {
                WalletID wid;
                wid.m_Pk = it->get_ParentObj().m_Pk;
                wid.m_Channel = it->m_Value;
                m_Wallet.OnWalletMessage(wid, std::move(msgWallet));
                break;
            }
        }
    }

    void BaseMessageEndpoint::AddOwnAddress(const WalletAddress& address)
    {
        Addr::Wid key;
        key.m_OwnID = address.m_OwnID;

        Addr* pAddr = nullptr;
        auto itW = m_Addresses.find(key);

        if (m_Addresses.end() == itW)
        {
            pAddr = new Addr;
            pAddr->m_ExpirationTime = address.getExpirationTime();
            pAddr->m_Wid.m_OwnID = address.m_OwnID;

            if (m_WalletDB->get_MasterKdf())
            {
                m_WalletDB->get_MasterKdf()->DeriveKey(pAddr->m_sk, Key::ID(address.m_OwnID, Key::Type::Bbs));

                proto::Sk2Pk(pAddr->m_Pk, pAddr->m_sk); // needed to "normalize" the sk, and calculate the channel
            }

            pAddr->m_Channel.m_Value = channel_from_wallet_id(address.m_walletID);

            m_Addresses.insert(pAddr->m_Wid);
            m_Channels.insert(pAddr->m_Channel);
        }
        else
        {
            pAddr = &(itW->get_ParentObj());
            pAddr->m_ExpirationTime = address.getExpirationTime();
        }

        if (pAddr && IsSingleChannelUser(pAddr->m_Channel))
        {
            OnChannelAdded(pAddr->m_Channel.m_Value);
        }

        LOG_INFO() << "WalletID " << to_string(address.m_walletID) << " subscribes to BBS channel " << pAddr->m_Channel.m_Value;
    }

    void BaseMessageEndpoint::DeleteOwnAddress(uint64_t ownID)
    {
        Addr::Wid key;
        key.m_OwnID = ownID;

        auto it = m_Addresses.find(key);
        if (m_Addresses.end() != it)
            DeleteAddr(it->get_ParentObj());
    }

    void BaseMessageEndpoint::DeleteAddr(const Addr& v)
    {
        if (IsSingleChannelUser(v.m_Channel))
        {
            OnChannelDeleted(v.m_Channel.m_Value);
        }

        m_Addresses.erase(WidSet::s_iterator_to(v.m_Wid));
        m_Channels.erase(ChannelSet::s_iterator_to(v.m_Channel));
        delete& v;
    }

    bool BaseMessageEndpoint::IsSingleChannelUser(const Addr::Channel& c)
    {
        ChannelSet::const_iterator it = ChannelSet::s_iterator_to(c);
        ChannelSet::const_iterator it2 = it;
        if (((++it2) != m_Channels.end()) && (it2->m_Value == c.m_Value))
            return false;

        if (it != m_Channels.begin())
        {
            it2 = it;
            if ((--it2)->m_Value == it->m_Value)
                return false;
        }

        return true;
    }

    void BaseMessageEndpoint::Send(const WalletID& peerID, const SetTxParameter& msg)
    {
        Serializer ser;
        ser& msg;
        SerializeBuffer sb = ser.buffer();

        ECC::NoLeak<ECC::Hash::Value> hvRandom;
        ECC::GenRandom(hvRandom.V);

        ECC::Scalar::Native nonce;
        m_WalletDB->get_MasterKdf()->DeriveKey(nonce, hvRandom.V);

        ByteBuffer encryptedMessage;
        if (proto::Bbs::Encrypt(encryptedMessage, peerID.m_Pk, nonce, sb.first, static_cast<uint32_t>(sb.second)))
        {
            SendEncryptedMessage(peerID, encryptedMessage);
        }
        else
        {
            LOG_WARNING() << "BBS serialization failed (bad peerID?)";
        }
    }

    void BaseMessageEndpoint::OnAddressTimer()
    {
        vector<Addr*> addressesToDelete;
        for (const auto& address : m_Addresses)
        {
            if (address.get_ParentObj().IsExpired())
            {
                addressesToDelete.push_back(&address.get_ParentObj());
            }
        }
        for (const auto& address : addressesToDelete)
        {
            DeleteAddr(*address);
        }
        m_AddressExpirationTimer->start(AddressUpdateInterval_ms, false, [this] { OnAddressTimer(); });
    }

    ///////////////////////////

    WalletNetworkViaBbs::WalletNetworkViaBbs(IWallet& w, shared_ptr<proto::FlyClient::INetwork> net, const IWalletDB::Ptr& pWalletDB)
        : BaseMessageEndpoint(w, pWalletDB)
        , m_NodeEndpoint(net)
		, m_WalletDB(pWalletDB)
	{
		ByteBuffer buffer;
		m_WalletDB->getBlob(BBS_TIMESTAMPS, buffer);
		if (!buffer.empty())
		{
			Deserializer d;
			d.reset(buffer.data(), buffer.size());

			d & m_BbsTimestamps;
		}

        Subscribe();
        m_WalletDB->subscribe(this);
	}

	WalletNetworkViaBbs::~WalletNetworkViaBbs()
	{
        m_WalletDB->unsubscribe(this);
		m_Miner.Stop();

		while (!m_PendingBbsMsgs.empty())
			DeleteReq(m_PendingBbsMsgs.front());

        Unsubscribe();

		try {
			SaveBbsTimestamps();
		} 
        catch (const std::exception& e)
        {
            LOG_UNHANDLED_EXCEPTION() << "what = " << e.what();
        }
        catch (...) {
            LOG_UNHANDLED_EXCEPTION();
		}
	}

	void WalletNetworkViaBbs::SaveBbsTimestamps()
	{
		Timestamp tsThreshold = getTimestamp() - 3600 * 24 * 3;

		for (auto it = m_BbsTimestamps.begin(); m_BbsTimestamps.end() != it; )
		{
			auto it2 = it++;
			if (it2->second < tsThreshold)
				m_BbsTimestamps.erase(it2);
		}

		Serializer s;
		s & m_BbsTimestamps;

		ByteBuffer buffer;
		s.swap_buf(buffer);

		m_WalletDB->setVarRaw(BBS_TIMESTAMPS, buffer.data(), static_cast<int>(buffer.size()));
	}

	void WalletNetworkViaBbs::DeleteReq(MyRequestBbsMsg& r)
	{
		m_PendingBbsMsgs.erase(BbsMsgList::s_iterator_to(r));
		r.m_pTrg = NULL;
		r.Release();
	}

	void WalletNetworkViaBbs::OnTimerBbsTmSave()
	{
		m_pTimerBbsTmSave.reset();
		SaveBbsTimestamps();
	}

	void WalletNetworkViaBbs::BbsSentEvt::OnComplete(proto::FlyClient::Request& r)
	{
		assert(r.get_Type() == proto::FlyClient::Request::Type::BbsMsg);
		get_ParentObj().DeleteReq(static_cast<MyRequestBbsMsg&>(r));
	}

	void WalletNetworkViaBbs::BbsSentEvt::OnMsg(proto::BbsMsg&& msg)
	{
		get_ParentObj().OnMsg(msg);
	}

	void WalletNetworkViaBbs::OnMsg(const proto::BbsMsg& msg)
	{
		if (msg.m_Message.empty())
			return;

		auto itBbs = m_BbsTimestamps.find(msg.m_Channel);
		if (m_BbsTimestamps.end() != itBbs)
        {
			itBbs->second = std::max(itBbs->second, msg.m_TimePosted);
        }
		else
        {
			m_BbsTimestamps[msg.m_Channel] = msg.m_TimePosted;
        }

		if (!m_pTimerBbsTmSave)
		{
			m_pTimerBbsTmSave = io::Timer::create(io::Reactor::get_Current());
			m_pTimerBbsTmSave->start(60*1000, false, [this]() { OnTimerBbsTmSave(); });
		}

        ProcessMessage(msg.m_Channel, msg.m_Message);
	}

    void WalletNetworkViaBbs::SendEncryptedMessage(const WalletID& peerID, const ByteBuffer& msg)
    {
        std::stringstream stream;
        stream << "0x" << std::setfill ('0') << std::setw(2) << std::hex;

        for (auto b : msg) {
            stream << std::setw(2) << int(b);
        }

        std::string str = stream.str(); // as string

        fprintf(stdout, "SendMsg 0x%x %s \n\n", (unsigned int) channel_from_wallet_id(peerID), str.c_str());

        Miner::Task::Ptr pTask = std::make_shared<Miner::Task>();
        pTask->m_Msg.m_Message = msg;
        
        pTask->m_Done = false;
        pTask->m_Msg.m_Channel = channel_from_wallet_id(peerID);

        if (m_MineOutgoing)
        {
            proto::Bbs::get_HashPartial(pTask->m_hpPartial, pTask->m_Msg);

            if (!m_Miner.m_pEvt)
            {
                m_Miner.m_pEvt = io::AsyncEvent::create(io::Reactor::get_Current(), [this]() { OnMined(); });
                m_Miner.m_Shutdown = false;

                uint32_t nThreads = std::thread::hardware_concurrency();
                nThreads = (nThreads > 1) ? (nThreads - 1) : 1; // leave at least 1 vacant core for other things
                m_Miner.m_vThreads.resize(nThreads);

                for (uint32_t i = 0; i < nThreads; i++)
                    m_Miner.m_vThreads[i] = std::thread(&Miner::Thread, &m_Miner, i);
            }

            std::unique_lock<std::mutex> scope(m_Miner.m_Mutex);

            m_Miner.m_Pending.push_back(std::move(pTask));
            m_Miner.m_NewTask.notify_all();
        }
        else
        {
            pTask->m_Msg.m_TimePosted = getTimestamp();
            OnMined(std::move(pTask->m_Msg));
        }
        
    }

    void WalletNetworkViaBbs::OnChannelAdded(BbsChannel channel)
	{
        Timestamp ts = 0;
        auto it = m_BbsTimestamps.find(channel);
        if (m_BbsTimestamps.end() != it)
            ts = it->second;

        m_NodeEndpoint->BbsSubscribe(channel, ts, &m_BbsSentEvt);
	}

    void WalletNetworkViaBbs::OnChannelDeleted(BbsChannel channel)
    {
        m_NodeEndpoint->BbsSubscribe(channel, 0, nullptr);
	}

	void WalletNetworkViaBbs::OnMined()
	{
		while (true)
		{
			Miner::Task::Ptr pTask;
			{
				std::unique_lock<std::mutex> scope(m_Miner.m_Mutex);

				if (!m_Miner.m_Done.empty())
				{
					pTask = std::move(m_Miner.m_Done.front());
					m_Miner.m_Done.pop_front();
				}
			}

			if (!pTask)
				break;

			OnMined(std::move(pTask->m_Msg));
		}
	}

	void WalletNetworkViaBbs::OnMined(proto::BbsMsg&& msg)
	{
		MyRequestBbsMsg::Ptr pReq(new MyRequestBbsMsg);

		pReq->m_Msg = std::move(msg);

		m_PendingBbsMsgs.push_back(*pReq);
		pReq->AddRef();

        m_NodeEndpoint->PostRequest(*pReq, m_BbsSentEvt);
	}

    void WalletNetworkViaBbs::onAddressChanged(ChangeAction action, const vector<WalletAddress>& items)
    {
        switch (action)
        {
        case ChangeAction::Added:
        case ChangeAction::Updated:
            for (const auto& address : items)
            {
                if (!address.isExpired())
                {
                    AddOwnAddress(address);
                }
                else
                {
                    DeleteOwnAddress(address.m_OwnID);
                }
            }
            break;
        case ChangeAction::Removed:
            for (const auto& address : items)
            {
                DeleteOwnAddress(address.m_OwnID);
            }
            break;
        case ChangeAction::Reset:
            assert(false && "invalid address change action");
            break;
        }
    }

	void WalletNetworkViaBbs::Miner::Stop()
	{
		if (!m_vThreads.empty())
		{
			{
				std::unique_lock<std::mutex> scope(m_Mutex);
				m_Shutdown = true;
				m_NewTask.notify_all();
			}

			for (size_t i = 0; i < m_vThreads.size(); i++)
				if (m_vThreads[i].joinable())
					m_vThreads[i].join();

			m_vThreads.clear();
			m_pEvt.reset();
		}
	}

	void WalletNetworkViaBbs::Miner::Thread(uint32_t iThread)
	{
		proto::Bbs::NonceType nStep = static_cast<uint32_t>(m_vThreads.size());

		while (true)
		{
			Task::Ptr pTask;

			for (std::unique_lock<std::mutex> scope(m_Mutex); ; m_NewTask.wait(scope))
			{
				if (m_Shutdown)
					return;

				if (!m_Pending.empty())
				{
					pTask = m_Pending.front();
					break;
				}
			}

			Timestamp ts = 0;
			proto::Bbs::NonceType nonce = iThread;
			bool bSuccess = false;

			for (uint32_t i = 0; ; i++)
			{
				if (pTask->m_Done || m_Shutdown)
					break;

				if (!(i & 0xff))
					ts = getTimestamp();

				// attempt to mine it
				ECC::Hash::Value hv;
				ECC::Hash::Processor hp = pTask->m_hpPartial;
				hp
					<< ts
					<< nonce
					>> hv;

				if (proto::Bbs::IsHashValid(hv))
				{
					bSuccess = true;
					break;
				}

				nonce += nStep;
			}

			if (bSuccess)
			{
				std::unique_lock<std::mutex> scope(m_Mutex);

				if (pTask->m_Done)
					bSuccess = false;
				else
				{
					assert(m_Pending.front() == pTask);

					pTask->m_Msg.m_TimePosted = ts;
					pTask->m_Msg.m_Nonce = nonce;

					pTask->m_Done = true;
					m_Pending.pop_front();
					m_Done.push_back(std::move(pTask));
				}
			}

			if (bSuccess)
				m_pEvt->post();
		}

	}

    /////////////////////////////////
    ColdWalletMessageEndpoint::ColdWalletMessageEndpoint(IWallet& wallet, IWalletDB::Ptr walletDB)
        : BaseMessageEndpoint(wallet, walletDB)
        , m_WalletDB(walletDB)
    {
        Subscribe();

        {
            auto messages = m_WalletDB->getIncomingWalletMessages();
            for (auto& message : messages)
            {
                ProcessMessage(message.m_Channel, message.m_Message);
                m_WalletDB->deleteIncomingWalletMessage(message.m_ID);
            }
        }
    }

    ColdWalletMessageEndpoint::~ColdWalletMessageEndpoint()
    {
        Unsubscribe();
    }

    void ColdWalletMessageEndpoint::SendEncryptedMessage(const WalletID& peerID, const ByteBuffer& msg)
    {
        m_WalletDB->saveWalletMessage(OutgoingWalletMessage{ 0, peerID, msg });
    }
}
