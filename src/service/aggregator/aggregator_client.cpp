
#include <nlohmann/json.hpp>
#include "aggregator_client.hpp"

using namespace std;
using json = nlohmann::json;

AggregatorClient::AggregatorClient (Goldilocks &fr, const Config &config, Prover &prover) :
    fr(fr),
    config(config),
    prover(prover)
{
    // Create channel
    std::shared_ptr<grpc_impl::Channel> channel = ::grpc::CreateChannel(config.aggregatorClientHost + ":" + to_string(config.aggregatorClientPort), grpc::InsecureChannelCredentials());

    // Create stub (i.e. client)
    stub = new aggregator::v1::AggregatorService::Stub(channel);
}

void AggregatorClient::runThread (void)
{
    cout << "AggregatorClient::runThread() creating aggregatorClientThread" << endl;
    pthread_create(&t, NULL, aggregatorClientThread, this);
}

void AggregatorClient::waitForThread (void)
{
    pthread_join(t, NULL);
}

bool AggregatorClient::GetStatus (::aggregator::v1::GetStatusResponse &getStatusResponse)
{
    // Lock the prover
    prover.lock();

    // Set last computed request data
    getStatusResponse.set_last_computed_request_id(prover.lastComputedRequestId);
    getStatusResponse.set_last_computed_end_time(prover.lastComputedRequestEndTime);

    // If computing, set the current request data
    if ((prover.pCurrentRequest != NULL) || (prover.pendingRequests.size() > 0))
    {
        getStatusResponse.set_status(aggregator::v1::GetStatusResponse_Status_COMPUTING);
        if (prover.pCurrentRequest != NULL)
        {
            getStatusResponse.set_current_computing_request_id(prover.pCurrentRequest->uuid);
            getStatusResponse.set_current_computing_start_time(prover.pCurrentRequest->startTime);
        }
        else
        {
            getStatusResponse.set_current_computing_request_id("");
            getStatusResponse.set_current_computing_start_time(0);
        }
    }
    else
    {
        getStatusResponse.set_status(aggregator::v1::GetStatusResponse_Status_IDLE);
        getStatusResponse.set_current_computing_request_id("");
        getStatusResponse.set_current_computing_start_time(0);
    }

    // Set the versions
    getStatusResponse.set_version_proto("v0_0_1");
    getStatusResponse.set_version_server("0.0.1");

    // Set the list of pending requests uuids
    for (uint64_t i=0; i<prover.pendingRequests.size(); i++)
    {
        getStatusResponse.add_pending_request_queue_ids(prover.pendingRequests[i]->uuid);
    }

    // Unlock the prover
    prover.unlock();

    // Set the prover id
    getStatusResponse.set_prover_id(config.processID);

    // Set the number of cores
    getStatusResponse.set_number_of_cores(getNumberOfCores());

    // Set the system memory details
    MemoryInfo memoryInfo;
    getMemoryInfo(memoryInfo);
    getStatusResponse.set_total_memory(memoryInfo.total);
    getStatusResponse.set_free_memory(memoryInfo.free);

#ifdef LOG_SERVICE
    cout << "AggregatorClient::GetStatus() returns: " << getStatusResponse.DebugString() << endl;
#endif
    return true;
}

bool AggregatorClient::GenProof (const aggregator::v1::GenProofRequest &genProofRequest, aggregator::v1::GenProofResponse &genProofResponse)
{
#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenProof() called with request: " << genProofRequest.DebugString() << endl;
#endif
    ProverRequest * pProverRequest = new ProverRequest(fr);
    if (pProverRequest == NULL)
    {
        cerr << "AggregatorClient::GenProof() failed allocation a new ProveRequest" << endl;
        exitProcess();
    }
#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenProof() created a new prover request: " << to_string((uint64_t)pProverRequest) << endl;
#endif

    // Set type to genProof
    pProverRequest->type = prt_genProof;

    // Parse public inputs
    aggregator::v1::PublicInputs publicInputs = genProofRequest.input().public_inputs();
    pProverRequest->input.publicInputs.oldStateRoot = publicInputs.old_state_root();
    if (pProverRequest->input.publicInputs.oldStateRoot.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenProof() got oldStateRoot too long, size=" << pProverRequest->input.publicInputs.oldStateRoot.size() << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.oldLocalExitRoot = publicInputs.old_local_exit_root();
    if (pProverRequest->input.publicInputs.oldLocalExitRoot.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenProof() got oldLocalExitRoot too long, size=" << pProverRequest->input.publicInputs.oldLocalExitRoot.size() << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.newStateRoot = publicInputs.new_state_root();
    if (pProverRequest->input.publicInputs.newStateRoot.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenProof() got newStateRoot too long, size=" << pProverRequest->input.publicInputs.newStateRoot.size() << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.newLocalExitRoot = publicInputs.new_local_exit_root();
    if (pProverRequest->input.publicInputs.newLocalExitRoot.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenProof() got newLocalExitRoot too long, size=" << pProverRequest->input.publicInputs.newLocalExitRoot.size() << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.sequencerAddr = publicInputs.sequencer_addr();
    if (pProverRequest->input.publicInputs.sequencerAddr.size() > (2 + 40))
    {
        cerr << "Error: AggregatorClient::GenProof() got sequencerAddr too long, size=" << pProverRequest->input.publicInputs.sequencerAddr.size() << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.batchHashData = publicInputs.batch_hash_data();
    if (pProverRequest->input.publicInputs.batchHashData.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenProof() got batchHashData too long, size=" << pProverRequest->input.publicInputs.batchHashData.size() << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.batchNum = publicInputs.batch_num();
    if (pProverRequest->input.publicInputs.batchNum == 0)
    {
        cerr << "Error: AggregatorClient::GenProof() got batch num = 0" << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.chainId = publicInputs.chain_id();
    if (pProverRequest->input.publicInputs.chainId == 0)
    {
        cerr << "Error: AggregatorClient::GenProof() got chainId = 0" << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.timestamp = publicInputs.eth_timestamp();

    // Parse aggregator address
    pProverRequest->input.publicInputs.aggregatorAddress = Add0xIfMissing(publicInputs.aggregator_addr());
    if (pProverRequest->input.publicInputs.aggregatorAddress.size() > (2 + 40))
    {
        cerr << "Error: AggregatorClient::GenProof() got aggregator address too long, size=" << pProverRequest->input.publicInputs.aggregatorAddress.size() << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }

    // Parse global exit root
    pProverRequest->input.globalExitRoot = genProofRequest.input().global_exit_root();
    if (pProverRequest->input.globalExitRoot.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenProof() got globalExitRoot too long, size=" << pProverRequest->input.globalExitRoot.size() << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }

    // Parse batch L2 data
    pProverRequest->input.batchL2Data = Add0xIfMissing(genProofRequest.input().batch_l2_data());

    // Parse aggregator address
    pProverRequest->input.publicInputs.aggregatorAddress = Add0xIfMissing(publicInputs.aggregator_addr());
    if (pProverRequest->input.publicInputs.aggregatorAddress.size() > (2 + 40))
    {
        cerr << "Error: AggregatorClient::GenProof() got aggregator address too long, size=" << pProverRequest->input.publicInputs.aggregatorAddress.size() << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }

    // Preprocess the transactions
    zkresult zkResult = pProverRequest->input.preprocessTxs();
    if (zkResult != ZKR_SUCCESS)
    {
        cerr << "Error: AggregatorClient::GenProof() failed calling pProverRequest.input.preprocessTxs() result=" << zkResult << "=" << zkresult2string(zkResult) << endl;
        genProofResponse.set_result(aggregator::v1::Result::ERROR);
#ifdef LOG_SERVICE
        cout << "AggregatorClient::GenProof() returns:\n" << genProofResponse.DebugString() << endl;
#endif
        return true;
    }

    // Parse keys map
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> > db;
    db = genProofRequest.input().db();
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> >::iterator it;
    for (it=db.begin(); it!=db.end(); it++)
    {
        if (it->first.size() > (64))
        {
            cerr << "Error: AggregatorClient::GenProof() got db key too long, size=" << it->first.size() << endl;
            genProofResponse.set_result(aggregator::v1::Result::ERROR);
            return false;
        }
        vector<Goldilocks::Element> dbValue;
        string concatenatedValues = it->second;
        if (concatenatedValues.size()%16!=0)
        {
            cerr << "Error: AggregatorClient::GenProof() found invalid db value size: " << concatenatedValues.size() << endl;
            genProofResponse.set_result(aggregator::v1::Result::ERROR);
            return false;
        }
        for (uint64_t i=0; i<concatenatedValues.size(); i+=16)
        {
            Goldilocks::Element fe;
            string2fe(fr, concatenatedValues.substr(i, 16), fe);
            dbValue.push_back(fe);
        }
        pProverRequest->input.db[it->first] = dbValue;
    }

    // Parse contracts data
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> > contractsBytecode;
    contractsBytecode = genProofRequest.input().contracts_bytecode();
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> >::iterator itp;
    for (itp=contractsBytecode.begin(); itp!=contractsBytecode.end(); itp++)
    {
        /*if (it->first.size() != (2+64))
        {
            cerr << "Error: ZKProverServiceImpl::GenProof() got contracts bytecode key too long, size=" << it->first.size() << endl;
            return Status::CANCELLED;
        }*/
        vector<uint8_t> dbValue;
        string contractValue = string2ba(itp->second);
        for (uint64_t i=0; i<contractValue.size(); i++)
        {
            dbValue.push_back(contractValue.at(i));
        }
        pProverRequest->input.contractsBytecode[itp->first] = dbValue;
    }

    // Submit the prover request
    string uuid = prover.submitRequest(pProverRequest);

    // Build the response as Ok, returning the UUID assigned by the prover to this request
    genProofResponse.set_result(aggregator::v1::Result::OK);
    genProofResponse.set_id(uuid.c_str());

#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenProof() returns: " << genProofResponse.DebugString() << endl;
#endif
    return true;
}

bool AggregatorClient::GenBatchProof (const aggregator::v1::GenBatchProofRequest &genBatchProofRequest, aggregator::v1::GenBatchProofResponse &genBatchProofResponse)
{
#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenBatchProof() called with request: " << genBatchProofRequest.DebugString() << endl;
#endif
    ProverRequest * pProverRequest = new ProverRequest(fr);
    if (pProverRequest == NULL)
    {
        cerr << "AggregatorClient::GenBatchProof() failed allocation a new ProveRequest" << endl;
        exitProcess();
    }
#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenBatchProof() created a new prover request: " << to_string((uint64_t)pProverRequest) << endl;
#endif

    // Set type to genBatchProof
    pProverRequest->type = prt_genBatchProof;

    // Parse public inputs
    aggregator::v1::PublicInputs publicInputs = genBatchProofRequest.input().public_inputs();
    pProverRequest->input.publicInputs.oldStateRoot = publicInputs.old_state_root();
    if (pProverRequest->input.publicInputs.oldStateRoot.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenBatchProof() got oldStateRoot too long, size=" << pProverRequest->input.publicInputs.oldStateRoot.size() << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.oldLocalExitRoot = publicInputs.old_local_exit_root();
    if (pProverRequest->input.publicInputs.oldLocalExitRoot.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenBatchProof() got oldLocalExitRoot too long, size=" << pProverRequest->input.publicInputs.oldLocalExitRoot.size() << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.newStateRoot = publicInputs.new_state_root();
    if (pProverRequest->input.publicInputs.newStateRoot.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenBatchProof() got newStateRoot too long, size=" << pProverRequest->input.publicInputs.newStateRoot.size() << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.newLocalExitRoot = publicInputs.new_local_exit_root();
    if (pProverRequest->input.publicInputs.newLocalExitRoot.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenBatchProof() got newLocalExitRoot too long, size=" << pProverRequest->input.publicInputs.newLocalExitRoot.size() << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.sequencerAddr = publicInputs.sequencer_addr();
    if (pProverRequest->input.publicInputs.sequencerAddr.size() > (2 + 40))
    {
        cerr << "Error: AggregatorClient::GenBatchProof() got sequencerAddr too long, size=" << pProverRequest->input.publicInputs.sequencerAddr.size() << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.batchHashData = publicInputs.batch_hash_data();
    if (pProverRequest->input.publicInputs.batchHashData.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenBatchProof() got batchHashData too long, size=" << pProverRequest->input.publicInputs.batchHashData.size() << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.batchNum = publicInputs.batch_num();
    if (pProverRequest->input.publicInputs.batchNum == 0)
    {
        cerr << "Error: AggregatorClient::GenBatchProof() got batch num = 0" << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.chainId = publicInputs.chain_id();
    if (pProverRequest->input.publicInputs.chainId == 0)
    {
        cerr << "Error: AggregatorClient::GenBatchProof() got chainId = 0" << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }
    pProverRequest->input.publicInputs.timestamp = publicInputs.eth_timestamp();

    // Parse aggregator address
    pProverRequest->input.publicInputs.aggregatorAddress = Add0xIfMissing(publicInputs.aggregator_addr());
    if (pProverRequest->input.publicInputs.aggregatorAddress.size() > (2 + 40))
    {
        cerr << "Error: AggregatorClient::GenBatchProof() got aggregator address too long, size=" << pProverRequest->input.publicInputs.aggregatorAddress.size() << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }

    // Parse global exit root
    pProverRequest->input.globalExitRoot = genBatchProofRequest.input().global_exit_root();
    if (pProverRequest->input.globalExitRoot.size() > (2 + 64))
    {
        cerr << "Error: AggregatorClient::GenBatchProof() got globalExitRoot too long, size=" << pProverRequest->input.globalExitRoot.size() << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }

    // Parse batch L2 data
    pProverRequest->input.batchL2Data = Add0xIfMissing(genBatchProofRequest.input().batch_l2_data());

    // Parse aggregator address
    pProverRequest->input.publicInputs.aggregatorAddress = Add0xIfMissing(publicInputs.aggregator_addr());
    if (pProverRequest->input.publicInputs.aggregatorAddress.size() > (2 + 40))
    {
        cerr << "Error: AggregatorClient::GenBatchProof() got aggregator address too long, size=" << pProverRequest->input.publicInputs.aggregatorAddress.size() << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }

    // Preprocess the transactions
    zkresult zkResult = pProverRequest->input.preprocessTxs();
    if (zkResult != ZKR_SUCCESS)
    {
        cerr << "Error: AggregatorClient::GenBatchProof() failed calling pProverRequest.input.preprocessTxs() result=" << zkResult << "=" << zkresult2string(zkResult) << endl;
        genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
#ifdef LOG_SERVICE
        cout << "AggregatorClient::GenBatchProof() returns:\n" << genBatchProofResponse.DebugString() << endl;
#endif
        return true;
    }

    // Parse keys map
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> > db;
    db = genBatchProofRequest.input().db();
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> >::iterator it;
    for (it=db.begin(); it!=db.end(); it++)
    {
        if (it->first.size() > (64))
        {
            cerr << "Error: AggregatorClient::GenBatchProof() got db key too long, size=" << it->first.size() << endl;
            genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
            return false;
        }
        vector<Goldilocks::Element> dbValue;
        string concatenatedValues = it->second;
        if (concatenatedValues.size()%16!=0)
        {
            cerr << "Error: AggregatorClient::GenBatchProof() found invalid db value size: " << concatenatedValues.size() << endl;
            genBatchProofResponse.set_result(aggregator::v1::Result::ERROR);
            return false;
        }
        for (uint64_t i=0; i<concatenatedValues.size(); i+=16)
        {
            Goldilocks::Element fe;
            string2fe(fr, concatenatedValues.substr(i, 16), fe);
            dbValue.push_back(fe);
        }
        pProverRequest->input.db[it->first] = dbValue;
    }

    // Parse contracts data
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> > contractsBytecode;
    contractsBytecode = genBatchProofRequest.input().contracts_bytecode();
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> >::iterator itp;
    for (itp=contractsBytecode.begin(); itp!=contractsBytecode.end(); itp++)
    {
        /*if (it->first.size() != (2+64))
        {
            cerr << "Error: ZKProverServiceImpl::GenProof() got contracts bytecode key too long, size=" << it->first.size() << endl;
            return Status::CANCELLED;
        }*/
        vector<uint8_t> dbValue;
        string contractValue = string2ba(itp->second);
        for (uint64_t i=0; i<contractValue.size(); i++)
        {
            dbValue.push_back(contractValue.at(i));
        }
        pProverRequest->input.contractsBytecode[itp->first] = dbValue;
    }

    // Submit the prover request
    string uuid = prover.submitRequest(pProverRequest);

    // Build the response as Ok, returning the UUID assigned by the prover to this request
    genBatchProofResponse.set_result(aggregator::v1::Result::OK);
    genBatchProofResponse.set_id(uuid.c_str());

#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenBatchProof() returns: " << genBatchProofResponse.DebugString() << endl;
#endif
    return true;
}

bool AggregatorClient::GenAggregatedProof (const aggregator::v1::GenAggregatedProofRequest &genAggregatedProofRequest, aggregator::v1::GenAggregatedProofResponse &genAggregatedProofResponse)
{
#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenAggregatedProof() called with request: " << genAggregatedProofRequest.DebugString() << endl;
#endif
    ProverRequest * pProverRequest = new ProverRequest(fr);
    if (pProverRequest == NULL)
    {
        cerr << "AggregatorClient::GenAggregatedProof() failed allocation a new ProveRequest" << endl;
        exitProcess();
    }
#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenAggregatedProof() created a new prover request: " << to_string((uint64_t)pProverRequest) << endl;
#endif

    // Set type to genAggregatedProof
    pProverRequest->type = prt_genAggregatedProof;

    // Set the 2 inputs
    pProverRequest->aggregatedProofInput1 = genAggregatedProofRequest.input_1();
    pProverRequest->aggregatedProofInput2 = genAggregatedProofRequest.input_2();

    // Submit the prover request
    string uuid = prover.submitRequest(pProverRequest);

    // Build the response as Ok, returning the UUID assigned by the prover to this request
    genAggregatedProofResponse.set_result(aggregator::v1::Result::OK);
    genAggregatedProofResponse.set_id(uuid.c_str());

#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenAggregatedProof() returns: " << genAggregatedProofResponse.DebugString() << endl;
#endif
    return true;
}

bool AggregatorClient::GenFinalProof (const aggregator::v1::GenFinalProofRequest &genFinalProofRequest, aggregator::v1::GenFinalProofResponse &genFinalProofResponse)
{
#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenFinalProof() called with request: " << genFinalProofRequest.DebugString() << endl;
#endif
    ProverRequest * pProverRequest = new ProverRequest(fr);
    if (pProverRequest == NULL)
    {
        cerr << "AggregatorClient::GenFinalProof() failed allocation a new ProveRequest" << endl;
        exitProcess();
    }
#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenFinalProof() created a new prover request: " << to_string((uint64_t)pProverRequest) << endl;
#endif

    // Set type to genFinalProof
    pProverRequest->type = prt_genFinalProof;

    // Set the 2 inputs
    pProverRequest->finalProofInput = genFinalProofRequest.input();

    // Submit the prover request
    string uuid = prover.submitRequest(pProverRequest);

    // Build the response as Ok, returning the UUID assigned by the prover to this request
    genFinalProofResponse.set_result(aggregator::v1::Result::OK);
    genFinalProofResponse.set_id(uuid.c_str());

#ifdef LOG_SERVICE
    cout << "AggregatorClient::GenFinalProof() returns: " << genFinalProofResponse.DebugString() << endl;
#endif
    return true;
}

bool AggregatorClient::Cancel (const aggregator::v1::CancelRequest &cancelRequest, aggregator::v1::CancelResponse &cancelResponse)
{
    // Get the cancel request UUID
    string uuid = cancelRequest.id();

    // Lock the prover
    prover.lock();

    // Map uuid to the corresponding prover request
    std::unordered_map<std::string, ProverRequest *>::iterator it = prover.requestsMap.find(uuid);
    if (it == prover.requestsMap.end())
    {
        prover.unlock();
        cerr << "AggregatorClient::Cancel() unknown uuid: " << uuid << endl;
        cancelResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }

    // Check if it is already completed
    if (it->second->bCompleted)
    {
        prover.unlock();
        cerr << "AggregatorClient::Cancel() already completed uuid: " << uuid << endl;
        cancelResponse.set_result(aggregator::v1::Result::ERROR);
        return false;
    }

    // Mark the request as cancelling
    it->second->bCancelling = true;

    // Unlock the prover
    prover.unlock();

    cancelResponse.set_result(aggregator::v1::Result::OK);

#ifdef LOG_SERVICE
    cout << "AggregatorClient::Cancel() returns: " << cancelResponse.DebugString() << endl;
#endif
    return true;
}

bool AggregatorClient::GetProof (const aggregator::v1::GetProofRequest &getProofRequest, aggregator::v1::GetProofResponse &getProofResponse)
{
#ifdef LOG_SERVICE
    cout << "AggregatorClient::GetProof() received request: " << getProofRequest.DebugString();
#endif
    // Get the prover request UUID from the request
    string uuid = getProofRequest.id();

    // Lock the prover
    prover.lock();

    // Map uuid to the corresponding prover request
    std::unordered_map<std::string, ProverRequest *>::iterator it = prover.requestsMap.find(uuid);

    // If UUID is not found, return the proper error
    if (it == prover.requestsMap.end())
    {
        cerr << "AggregatorClient::GetProof() invalid uuid:" << uuid << endl;
        getProofResponse.set_result(aggregator::v1::GetProofResponse_Result_ERROR);
        getProofResponse.set_result_string("invalid UUID");
    }
    else
    {
        ProverRequest * pProverRequest = it->second;

        // If request is not completed, return the proper result
        if (!pProverRequest->bCompleted)
        {
            //cerr << "ZKProverServiceImpl::GetProof() not completed uuid=" << uuid << endl;
            getProofResponse.set_result(aggregator::v1::GetProofResponse_Result_PENDING);
            getProofResponse.set_result_string("pending");
        }
        // If request is completed, return the proof
        else
        {
            // Request is completed
            getProofResponse.set_id(uuid);
            if (pProverRequest->result != ZKR_SUCCESS)
            {
                getProofResponse.set_result(aggregator::v1::GetProofResponse_Result_COMPLETED_ERROR);
                getProofResponse.set_result_string("completed_error");
            }
            else
            {
                getProofResponse.set_result(aggregator::v1::GetProofResponse_Result_COMPLETED_OK);
                getProofResponse.set_result_string("completed");
            }

            switch (pProverRequest->type)
            {
                case prt_genProof:
                case prt_genFinalProof:
                {
                    // Convert the returned Proof to aggregator::Proof

                    aggregator::v1::Proof * pProofProver = new aggregator::v1::Proof();

                    // Set proofA
                    for (uint64_t i=0; i<pProverRequest->proof.proofA.size(); i++)
                    {
                        pProofProver->add_proof_a(pProverRequest->proof.proofA[i]);
                    }

                    // Set proofB
                    for (uint64_t i=0; i<pProverRequest->proof.proofB.size(); i++)
                    {
                        aggregator::v1::ProofB *pProofB = pProofProver->add_proof_b();
                        for (uint64_t j=0; j<pProverRequest->proof.proofB[i].proof.size(); j++)
                        {
                            pProofB->add_proofs(pProverRequest->proof.proofB[i].proof[j]);
                        }
                    }

                    // Set proofC
                    for (uint64_t i=0; i<pProverRequest->proof.proofC.size(); i++)
                    {
                        pProofProver->add_proof_c(pProverRequest->proof.proofC[i]);
                    }

                    getProofResponse.set_allocated_proof(pProofProver);

                    // Set public inputs extended
                    aggregator::v1::PublicInputsExtended* pPublicInputsExtended = new(aggregator::v1::PublicInputsExtended);
                    pPublicInputsExtended->set_input_hash(pProverRequest->proof.publicInputsExtended.inputHash);
                    aggregator::v1::PublicInputs* pPublicInputs = new(aggregator::v1::PublicInputs);
                    pPublicInputs->set_old_state_root(pProverRequest->proof.publicInputsExtended.publicInputs.oldStateRoot);
                    pPublicInputs->set_old_local_exit_root(pProverRequest->proof.publicInputsExtended.publicInputs.oldLocalExitRoot);
                    pPublicInputs->set_new_state_root(pProverRequest->proof.publicInputsExtended.publicInputs.newStateRoot);
                    pPublicInputs->set_new_local_exit_root(pProverRequest->proof.publicInputsExtended.publicInputs.newLocalExitRoot);
                    pPublicInputs->set_sequencer_addr(pProverRequest->proof.publicInputsExtended.publicInputs.sequencerAddr);
                    pPublicInputs->set_batch_hash_data(pProverRequest->proof.publicInputsExtended.publicInputs.batchHashData);
                    pPublicInputs->set_batch_num(pProverRequest->proof.publicInputsExtended.publicInputs.batchNum);
                    pPublicInputs->set_eth_timestamp(pProverRequest->proof.publicInputsExtended.publicInputs.timestamp);
                    pPublicInputsExtended->set_allocated_public_inputs(pPublicInputs);
                    getProofResponse.set_allocated_public_(pPublicInputsExtended);
                    
                    break;
                }
                case prt_genBatchProof:
                {
                    string output = pProverRequest->batchProofOutput.dump();
                    getProofResponse.set_output(output);
                    break;
                }
                case prt_genAggregatedProof:
                {
                    string output = pProverRequest->aggregatedProofOutput.dump();
                    getProofResponse.set_output(output);
                    break;
                }
                default:
                {
                    cerr << "AggregatorClient::GetProof() invalid pProverRequest->type=" << pProverRequest->type << endl;
                    exitProcess();
                }
            }
        }
    }
    
    prover.unlock();

#ifdef LOG_SERVICE
    cout << "AggregatorClient::GetProof() sends response: " << getProofResponse.DebugString();
#endif
    return true;
}

void* aggregatorClientThread(void* arg)
{
    cout << "aggregatorClientThread() started" << endl;
    string uuid;
    AggregatorClient *pAggregatorClient = (AggregatorClient *)arg;

    while (true)
    {
        ::grpc::ClientContext context;
        std::unique_ptr<grpc::ClientReaderWriter<aggregator::v1::ProverMessage, aggregator::v1::AggregatorMessage>> readerWriter;
        readerWriter = pAggregatorClient->stub->Channel(&context);
        bool bResult;
        while (true)
        {
            ::aggregator::v1::AggregatorMessage aggregatorMessage;
            ::aggregator::v1::ProverMessage proverMessage;

            // Read a new aggregator message
            bResult = readerWriter->Read(&aggregatorMessage);
            if (!bResult)
            {
                cerr << "aggregatorClientThread() failed calling readerWriter->Read(&aggregatorMessage)" << endl;
                break;
            }
            cout << "aggregatorClientThread() got: " << aggregatorMessage.ShortDebugString() << endl;

            // We return the same ID we got in the aggregator message
            proverMessage.set_id(aggregatorMessage.id());

            switch (aggregatorMessage.type())
            {
                case aggregator::v1::AggregatorMessage_Type::AggregatorMessage_Type_GET_STATUS_REQUEST:
                {
                    // Allocate a new get status response
                    aggregator::v1::GetStatusResponse * pGetStatusResponse = new aggregator::v1::GetStatusResponse();
                    zkassert(pGetStatusResponse != NULL);

                    // Call GetStatus
                    pAggregatorClient->GetStatus(*pGetStatusResponse);

                    // Set the get status response
                    proverMessage.set_allocated_get_status_response(pGetStatusResponse);

                    proverMessage.set_type(aggregator::v1::ProverMessage_Type_GET_STATUS_RESPONSE);
                    break;
                }

                case aggregator::v1::AggregatorMessage_Type::AggregatorMessage_Type_GEN_PROOF_REQUEST:
                {
                    // Allocate a new gen proof response
                    aggregator::v1::GenProofResponse * pGenProofResponse = new aggregator::v1::GenProofResponse();
                    zkassert(pGenProofResponse != NULL);

                    // Call GenProof
                    pAggregatorClient->GenProof(aggregatorMessage.gen_proof_request(), *pGenProofResponse);

                    // Set the gen proof response
                    proverMessage.set_allocated_gen_proof_response(pGenProofResponse);

                    proverMessage.set_type(aggregator::v1::ProverMessage_Type_GEN_PROOF_RESPONSE);
                    break;
                }

                case aggregator::v1::AggregatorMessage_Type::AggregatorMessage_Type_GEN_BATCH_PROOF_REQUEST:
                {
                    // Allocate a new gen batch proof response
                    aggregator::v1::GenBatchProofResponse * pGenBatchProofResponse = new aggregator::v1::GenBatchProofResponse();
                    zkassert(pGenBatchProofResponse != NULL);

                    // Call GenBatchProof
                    pAggregatorClient->GenBatchProof(aggregatorMessage.gen_batch_proof_request(), *pGenBatchProofResponse);

                    // Set the gen batch proof response
                    proverMessage.set_allocated_gen_batch_proof_response(pGenBatchProofResponse);

                    proverMessage.set_type(aggregator::v1::ProverMessage_Type_GEN_BATCH_PROOF_RESPONSE);
                    break;
                }

                case aggregator::v1::AggregatorMessage_Type::AggregatorMessage_Type_GEN_AGGREGATED_PROOF_REQUEST:
                {
                    // Allocate a new gen aggregated proof response
                    aggregator::v1::GenAggregatedProofResponse * pGenAggregatedProofResponse = new aggregator::v1::GenAggregatedProofResponse();
                    zkassert(pGenAggregatedProofResponse != NULL);

                    // Call GenAggregatedProof
                    pAggregatorClient->GenAggregatedProof(aggregatorMessage.gen_aggregated_proof_request(), *pGenAggregatedProofResponse);

                    // Set the gen aggregated proof response
                    proverMessage.set_allocated_gen_aggregated_proof_response(pGenAggregatedProofResponse);

                    proverMessage.set_type(aggregator::v1::ProverMessage_Type_GEN_AGGREGATED_PROOF_RESPONSE);
                    break;
                }

                case aggregator::v1::AggregatorMessage_Type::AggregatorMessage_Type_GEN_FINAL_PROOF_REQUEST:
                {
                    // Allocate a new gen final proof response
                    aggregator::v1::GenFinalProofResponse * pGenFinalProofResponse = new aggregator::v1::GenFinalProofResponse();
                    zkassert(pGenFinalProofResponse != NULL);

                    // Call GenFinalProof
                    pAggregatorClient->GenFinalProof(aggregatorMessage.gen_final_proof_request(), *pGenFinalProofResponse);

                    // Set the gen final proof response
                    proverMessage.set_allocated_gen_final_proof_response(pGenFinalProofResponse);

                    proverMessage.set_type(aggregator::v1::ProverMessage_Type_GEN_FINAL_PROOF_RESPONSE);
                    break;
                }

                case aggregator::v1::AggregatorMessage_Type::AggregatorMessage_Type_CANCEL_REQUEST:
                {
                    // Allocate a new cancel response
                    aggregator::v1::CancelResponse * pCancelResponse = new aggregator::v1::CancelResponse();
                    zkassert(pCancelResponse != NULL);

                    // Call Cancel
                    pAggregatorClient->Cancel(aggregatorMessage.cancel_request(), *pCancelResponse);

                    // Set the cancel response
                    proverMessage.set_allocated_cancel_response(pCancelResponse);

                    proverMessage.set_type(aggregator::v1::ProverMessage_Type_CANCEL_RESPONSE);
                    break;
                }

                case aggregator::v1::AggregatorMessage_Type::AggregatorMessage_Type_GET_PROOF_REQUEST:
                {
                    // Allocate a new cancel response
                    aggregator::v1::GetProofResponse * pGetProofResponse = new aggregator::v1::GetProofResponse();
                    zkassert(pGetProofResponse != NULL);

                    // Call GetProof
                    pAggregatorClient->GetProof(aggregatorMessage.get_proof_request(), *pGetProofResponse);

                    // Set the get proof response
                    proverMessage.set_allocated_get_proof_response(pGetProofResponse);

                    proverMessage.set_type(aggregator::v1::ProverMessage_Type_GET_PROOF_RESPONSE);
                    break;
                }

                default:
                {
                    cerr << "aggregatorClientThread() received an invalid type=" << aggregatorMessage.type() << endl;
                    break;
                }
            }

            // Write the prover message
            bResult = readerWriter->Write(proverMessage);
            if (!bResult)
            {
                cerr << "aggregatorClientThread() failed calling readerWriter->Write(proverMessage)" << endl;
                break;
            }
            cout << "aggregatorClientThread() sent: " << proverMessage.ShortDebugString() << endl;
        }
        cout << "aggregatorClientThread() channel broken; will retry in 5 seconds" << endl;
        sleep(5);
    }
    return NULL;
}