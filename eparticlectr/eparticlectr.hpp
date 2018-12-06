// # 2018 Travis Moore, Kedar Iyer, Sam Kazemian
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMmdhhydNMMMMMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMNdy    hMMMMMMNdhhmMMMddMMMMMMMMMMMMM
// # MMMMMMMMMMMmh      hMMMMMMh     yMMM  hNMMMMMMMMMM
// # MMMMMMMMMNy       yMMMMMMh       MMMh   hNMMMMMMMM
// # MMMMMMMMd         dMMMMMM       hMMMh     NMMMMMMM
// # MMMMMMMd          dMMMMMN      hMMMm       mMMMMMM
// # MMMMMMm           yMMMMMM      hmmh         NMMMMM
// # MMMMMMy            hMMMMMm                  hMMMMM
// # MMMMMN             hNMMMMMmy                 MMMMM
// # MMMMMm          ymMMMMMMMMmd                 MMMMM
// # MMMMMm         dMMMMMMMMd                    MMMMM
// # MMMMMMy       mMMMMMMMm                     hMMMMM
// # MMMMMMm      dMMMMMMMm                      NMMMMM
// # MMMMMMMd     NMMMMMMM                      mMMMMMM
// # MMMMMMMMd    NMMMMMMN                     mMMMMMMM
// # MMMMMMMMMNy  mMMMMMMM                   hNMMMMMMMM
// # MMMMMMMMMMMmyyNMMMMMMm         hmh    hNMMMMMMMMMM
// # MMMMMMMMMMMMMNmNMMMMMMMNmdddmNNd   ydNMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMNdhyhdmMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMNNMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM
// # MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM

#include <eosiolib/eosio.hpp>
#include <ctime>

using namespace eosio;

const name ARTICLE_CONTRACT_ACCTNAME = name("eparticlectr");
const name TOKEN_CONTRACT_ACCTNAME = name("everipediaiq");
const uint64_t IQ_TO_BRAINPOWER_RATIO = 1;
const uint64_t STAKING_DURATION = 21 * 86400; // 21 days
const uint64_t EDIT_PROPOSE_BRAINPOWER = 50;
const uint32_t REWARD_INTERVAL = 1800; // 30 min
const uint32_t DEFAULT_VOTING_TIME = 21600; // 6 hours
const uint64_t IQ_PRECISION_MULTIPLIER = 1000;
const float TIER_ONE_THRESHOLD = 0.5f;
uint64_t PERIOD_CURATION_REWARD = 100000; // 100 IQ per period 
uint64_t PERIOD_EDITOR_REWARD = 400000; // 400 IQ per period 

class [[eosio::contract("eparticlectr")]] eparticlectr : public contract {
    using contract::contract;

private:
    using ipfshash_t = std::string;
    enum ProposalStatus { pending, accepted, rejected };

public:
    eosio::symbol IQSYMBOL = symbol(symbol_code("IQ"), 3);
    static fixed_bytes<32> ipfs_to_fixed_bytes32(const ipfshash_t& input) {
        fixed_bytes<32> returnKey;
        if (input == "") {
            returnKey = fixed_bytes<32>::make_from_word_sequence<uint64_t>(0ULL, 0ULL, 0ULL, 0ULL);
        }
        else {
            // This is needed for indexing since indexes cannot be done by strings, only max fixed_bytes<32>'s, for now...
            uint64_t p1 = name(input.substr(0, 12)).value;
            uint64_t p2 = name(input.substr(13, 24)).value;
            uint64_t p3 = name(input.substr(25, 36)).value;
            uint64_t p4 = name(input.substr(37, 45)).value;
            returnKey = fixed_bytes<32>::make_from_word_sequence<uint64_t>(p1, p2, p3, p4);
        }
        return returnKey;
    }

    // This is until secondary keys get fixed with cleos get table :)
    static uint64_t ipfs_to_uint64_trunc(const ipfshash_t& input) {
        ipfshash_t newHash = input;
        char chars[] = "6789";
        for (unsigned int i = 0; i < strlen(chars); ++i)
        {
           newHash.erase(std::remove(newHash.begin(), newHash.end(), chars[i]), newHash.end());
        }
        ipfshash_t truncatedHash = newHash.substr(2,12);
        transform(truncatedHash.begin(), truncatedHash.end(), truncatedHash.begin(), ::tolower);
        const char* cstringedMiniHash = truncatedHash.c_str();
        // print(cstringedMiniHash, "\n");
        uint64_t hashNumber = name(cstringedMiniHash).value;
        // print("Before: ", hashNumber, "\n");
        hashNumber = hashNumber % 9007199254740990; // Max safe javascript integer
        // print("After: ", hashNumber, "\n");
        return(hashNumber);
    }

private:
    // ==================================================
    // ==================================================
    // ==================================================
    // DATABASE SCHEMAS
    // Wiki articles
    // @abi table
    struct wiki {
          uint64_t id;
          ipfshash_t hash; // IPFS hash of the current consensus article version
          ipfshash_t parent_hash; // IPFS hash of the parent article version

          uint64_t primary_key () const { return id; }
          fixed_bytes<32> get_hash_fixed_bytes32 () const { return eparticlectr::ipfs_to_fixed_bytes32(hash); }
          fixed_bytes<32> get_parent_hash_fixed_bytes32 () const { return eparticlectr::ipfs_to_fixed_bytes32(parent_hash); }
    };

    // Internal struct for stakes within brainpower
    // @abi table
    struct stake {
        uint64_t id;
        name user;
        uint64_t deleteme; // required to maintain old schema
        uint64_t amount;
        uint32_t timestamp;
        uint32_t completion_time;
        bool autorenew = 0;

        uint64_t primary_key()const { return id; }
        uint64_t get_user()const { return user.value; }
    };

    // Voting tally
    // @abi table
    struct vote {
          uint64_t id;
          uint64_t proposal_id;
          ipfshash_t proposed_article_hash; // IPFS hash of the proposed new version
          bool approve;
          bool is_editor;
          uint64_t amount;
          name voter; // account name of the voter
          uint32_t timestamp; // epoch time of the vote

          uint64_t primary_key()const { return id; }
          fixed_bytes<32> get_hash_fixed_bytes32 () const { return eparticlectr::ipfs_to_fixed_bytes32(proposed_article_hash); }
          uint64_t get_hash_uint64 () const { return eparticlectr::ipfs_to_uint64_trunc(proposed_article_hash); }
          uint64_t get_proposal_id() const { return id; }
          uint64_t get_voter()const { return voter.value; }
    };

    // Brainpower balances
    // @abi table
    struct brainpower {
        name user;
        uint64_t power = 0; // TODO: need to fix this later

        uint64_t primary_key()const { return user.value; }
        uint64_t get_power()const { return power; }

        // subtraction with underflow check
        uint64_t sub (uint64_t value) {
            eosio_assert(value != 0, "Please supply a nonzero value of brainpower to subtract");
            eosio_assert(value <= power, "Underflow during subtraction");
            power -= value;
            return power;
        }

        // addition with overflow check
        uint64_t add (uint64_t value) {
            eosio_assert(value != 0, "Please supply a nonzero value of brainpower to add");
            eosio_assert(value + power >= value && value + power > power, "Overflow during addition");
            power += value;
            print( "Added brainpower, ", name{power} );
            return power;
        }
    };


    // Edit Proposals
    // @abi table
    struct editproposal {
          uint64_t id;
          ipfshash_t proposed_article_hash; // IPFS hash of the proposed new version
          ipfshash_t old_article_hash; // IPFS hash of the old version
          ipfshash_t grandparent_hash; // IPFS hash of the grandparent hash
          name proposer; // account name of the proposer
          uint64_t deleteme; // required to maintain old schema
          uint32_t tier;
          uint32_t starttime; // epoch time of the proposal
          uint32_t endtime;
          uint32_t finalized_time; // when finalize() was called
          uint32_t status;

          uint64_t primary_key () const { return id; }
          fixed_bytes<32> get_hash_fixed_bytes32 () const { return eparticlectr::ipfs_to_fixed_bytes32(proposed_article_hash); }
          uint64_t get_hash_uint64 () const { return eparticlectr::ipfs_to_uint64_trunc(proposed_article_hash); }
          uint64_t get_finalize_period() const { return (finalized_time / REWARD_INTERVAL); } // truncate to the nearest period
          uint64_t get_proposer () const { return proposer.value; }

    };


    // Internal struct for history of success rewards and reject slashes
    // slashes will be done immediately at finalize(). Rewards will be done at 30min periods
    // @abi table
    struct rewardhistory {
        uint64_t id;
        name user;
        uint64_t amount; // slash or reward amount
        uint64_t approval_vote_sum; // sum of all "for" votes for this proposal
        uint64_t proposal_id; // id of the proposal that this person voted on
        ipfshash_t proposed_article_hash; // IPFS hash of the proposed new version
        uint32_t proposal_finalize_time; // when finalize() was called
        uint32_t proposal_finalize_period; // truncate to the nearest period
        bool proposalresult = 0;
        bool is_editor = 0;
        bool is_tie = 0;

        uint64_t primary_key()const { return id; }
        uint64_t get_user()const { return user.value; }
        uint64_t get_proposal()const { return proposal_id; }
        uint64_t get_finalize_period()const { return proposal_finalize_period; }
    };

    struct periodreward {
        uint64_t period;
      	uint64_t curation_sum;
      	uint64_t editor_sum;

        uint64_t primary_key() const { return period; }
    };

    //  ==================================================
    //  ==================================================
    //  ==================================================
    // DATABASE TABLES
    // GUIDE: https://github.com/EOSIO/eos/wiki/Persistence-API

    // wikis table
    // indexed by wiki hash
    // indexed by parent hash
    // @abi table
    typedef eosio::multi_index<name("wikistbl"), wiki,
        indexed_by< name("byhash"), const_mem_fun< wiki, fixed_bytes<32>, &wiki::get_hash_fixed_bytes32 >>,
        indexed_by< name("byoldhash"), const_mem_fun< wiki, fixed_bytes<32>, &wiki::get_parent_hash_fixed_bytes32 >>
    > wikistbl; // EOS table for the articles

    // stake table
    // @abi table
    typedef eosio::multi_index<name("staketbl"), stake,
        indexed_by< name("byuser"), const_mem_fun<stake, uint64_t, &stake::get_user >>
    > staketbl;

    // brainpower table
    // @abi table
    typedef eosio::multi_index<name("brainpwrtbl"), brainpower,
        indexed_by< name("power"), const_mem_fun< brainpower, uint64_t, &brainpower::get_power >>
    > brainpwrtbl;

    // votes table
    // indexed by proposal
    // @abi table
    typedef eosio::multi_index<name("votestbl"), vote,
        indexed_by< name("byhash"), const_mem_fun< vote, fixed_bytes<32>, &vote::get_hash_fixed_bytes32 >>,
        indexed_by< name("byhashtrunc"), const_mem_fun< vote, uint64_t, &vote::get_hash_uint64 >>,
        indexed_by< name("byproposal"), const_mem_fun< vote, uint64_t, &vote::get_proposal_id >>,
        indexed_by< name("byvoter"), const_mem_fun< vote, uint64_t, &vote::get_voter >>
    > votestbl; // EOS table for the votes

    // edit proposals table
    // 12-char limit on table names, so proposals used instead of editproposals
    // indexed by hash
    // @abi table
    typedef eosio::multi_index<name("propstbl"), editproposal,
        indexed_by< name("byhash"), const_mem_fun< editproposal, fixed_bytes<32>, &editproposal::get_hash_fixed_bytes32 >>,
        indexed_by< name("byhashtrunc"), const_mem_fun< editproposal, uint64_t, &editproposal::get_hash_uint64 >>,
        indexed_by< name("byfinalper"), const_mem_fun< editproposal, uint64_t, &editproposal::get_finalize_period >>
    > propstbl; // EOS table for the edit proposals


    // rewards history table
    // @abi table
    typedef eosio::multi_index<name("rewardstbl"), rewardhistory,
        indexed_by< name("byuser"), const_mem_fun<rewardhistory, uint64_t, &rewardhistory::get_user>>,
        indexed_by< name("byfinalper"), const_mem_fun<rewardhistory, uint64_t, &rewardhistory::get_finalize_period >>,
        indexed_by< name("byproposal"), const_mem_fun<rewardhistory, uint64_t, &rewardhistory::get_proposal >>
    > rewardstbl;

    // period rewards table
    // @abi table
    typedef eosio::multi_index<name("periodreward"), periodreward> perrwdstbl;


public:
    //  ==================================================
    //  ==================================================
    //  ==================================================
    // ABI Functions

    void brainclmid( uint64_t stakeid );

    void brainmeart( name staker,
                  uint64_t amount );

    void notify( name to,
                  std::string memo );

    void finalize( uint64_t proposal_id );

    void fnlbyhash( ipfshash_t& proposal_hash );

    void oldvotepurge( ipfshash_t& proposed_article_hash,
                       uint32_t loop_limit);

    void procrewards( uint64_t reward_period );

    void propose( name proposer,
                  ipfshash_t& proposed_article_hash,
                  ipfshash_t& old_article_hash,
                  ipfshash_t& grandparent_hash );

    void updatewiki( ipfshash_t& current_hash );

    void votebyhash ( name voter,
                      ipfshash_t& proposed_article_hash,
                      bool approve,
                      uint64_t amount );

    void rewardclmall ( name user );

    void rewardclmid ( uint64_t reward_id );

    void logpropres( ipfshash_t& proposal, bool approved, uint64_t yes_votes, uint64_t no_votes ); 
};
