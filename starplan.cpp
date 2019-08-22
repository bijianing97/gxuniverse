#include "starplan.hpp"

void starplan::init()
{
    //////////////////////////////////////// 对调用进行校验 /////////////////////////////////////////////////
    // 0、防止跨合约调用
    uint64_t sender_id = checkSender();

    // 1、校验调用者，只有调用者可以初始化底池，只许调用一次
    graphene_assert(sender_id == ADMIN_ID, MSG_CHECK_ADMIN);

    // 2、校验充值的资产是否为INIT_POOL的大小
    uint64_t amount = assetEqualCheck(INIT_POOL);

    // 3、校验底池是否已经初始化
    auto glo_itor = tbglobals.find(0);
    graphene_assert(glo_itor == tbglobals.end(),MSG_INIT_FAIL);

    // 4、校验当前轮资金池是否已经初始化
    auto rou_itor = tbrounds.find(0);
    graphene_assert(rou_itor == tbrounds.end(),MSG_INIT_ROUND_FAIL);

    //////////////////////////////////////// 校验通过后，初始化资金池 //////////////////////////////////////////
    // 5、初始化总资金池
    tbglobals.emplace(sender_id, [&](auto &obj) {
        obj.index           = 0;
        obj.pool_amount     = amount;
        obj.current_round   = 0;
        obj.upgrading       = 0;
    });
    // 6、初始化第一轮资金池，并启动第一轮
    tbrounds.emplace(sender_id, [&](auto &obj) {
        obj.round                   = tbrounds.available_primary_key();
        obj.current_round_invites   = 0;
        obj.actual_rewards          = 0;
        obj.base_pool_amount        = 0;
        obj.random_pool_amount      = 0;
        obj.invite_reward_amount    = 0;
        obj.start_time              = get_head_block_time();;
        obj.end_time                = 0;
    });
}

void starplan::vote(const std::string &inviter, const std::string &superstar)
{
    //////////////////////////////// 校验 ////////////////////////////////
    baseCheck();
    roundFinishCheck();

    uint64_t amount = assetLargerCheck(MIN_VOTE_AMOUNT);

    uint64_t sender_id = get_trx_origin();
    uint64_t inviter_id = inviterCheck(inviter, sender_id);

    uint64_t super_id = superStarCheck(superstar);

    //////////////////////////////// 执行 ////////////////////////////////
    invite(sender_id, inviter_id);

    uint64_t vote_id = createVote(sender_id, super_id, amount);
    createStaking(sender_id, amount, super_id, STAKING_TYPE_VOTE, vote_id);

    uint64_t totalVotes = updateAccountVote(sender_id, amount);
    if(totalVotes >= Y && !isSmallPlanet(sender_id))
        createSmallPlanet(sender_id);

    updateSuperstarVote(super_id, amount, sender_id);
}

void starplan::selfactivate(const std::string &superstar)
{
    //////////////////////////////// 校验 ////////////////////////////////
    baseCheck();
    roundFinishCheck();

    uint64_t amount = assetEqualCheck(Z + Z1 + Z2 + Z3);

    uint64_t sender_id = get_trx_origin();
    graphene_assert(isBigPlanet(sender_id) || superstarEnabled(sender_id), MSG_SELF_ACTIVATE_AUTH_ERR);

    auto super_id = superStarCheck(superstar);

    //////////////////////////////// 执行 ////////////////////////////////
    updateActivePlanet(sender_id, sender_id);

    uint64_t vote_id = createVote(sender_id, super_id, Z);

    createStaking(sender_id, Z, super_id, STAKING_TYPE_SELF_ACTIVATE, vote_id);

    updateAccountVote(sender_id, Z);

    updateSuperstarVote(super_id, Z, sender_id);

    distributeInviteRewards(sender_id, sender_id, RWD_TYPE_SELF_ACTIVATE);

    progress(sender_id);
}

void starplan::uptobig()
{
    //////////////////////////////// 校验 ////////////////////////////////
    baseCheck();
    roundFinishCheck();

    uint64_t amount = assetEqualCheck(Z1 + Z2 + Z3);

    uint64_t sender_id = get_trx_origin();
    graphene_assert(isSmallPlanet(sender_id), MSG_NOT_SMALL_PLANET);
    graphene_assert(!isBigPlanet(sender_id), MSG_ALREADY_BIG_PLANET);

    //////////////////////////////// 执行 ////////////////////////////////
    createBigPlanet(sender_id);

    activateInvite(sender_id);

    progress(sender_id);

    distributeInviteRewards(sender_id, getInviter(sender_id), RWD_TYPE_INVITE);

    auto invitee_idx = tbinvites.get_index<N(byinvitee)>();
    auto invitee_itor = invitee_idx.find(sender_id);
    if(invitee_itor != invitee_idx.end())
        updateActivePlanet(invitee_itor->inviter, invitee_itor->invitee);
}

void starplan::uptosuper(const std::string &inviter, const std::string &memo)
{
    //////////////////////////////// 校验 ////////////////////////////////
    baseCheck();
    roundFinishCheck();

    uint64_t amount = assetEqualCheck(X);

    uint64_t sender_id = get_trx_origin();
    uint64_t inviter_id = inviterCheck(inviter, sender_id);

    graphene_assert(memo.size() <= MAX_MEMO_LENGTH, MSG_MEMO_TOO_LONG);
    graphene_assert(!superstarEnabled(sender_id), MSG_ALREADY_SUPER_STAR);

    //////////////////////////////// 执行 ////////////////////////////////
    if(superstarExist(sender_id)) {
        enableSuperstar(sender_id, memo);
    } else {
        createSuperstar(sender_id, memo);
    }

    createStaking(sender_id, amount, sender_id, STAKING_TYPE_TO_SUPER);

    invite(sender_id, inviter_id);

    activateInvite(sender_id);

    progress(sender_id);

    updateActivePlanetForSuper(sender_id);
}

void starplan::claim(uint64_t stakingid)
{
    //TODO check 如果在一轮执行的过程中或者结算过程中调用会怎样？分小行星/大行星/超级星/活力星等情况分析
    baseCheck();

    auto itor = tbstakes.find(stakingid);
    graphene_assert(itor != tbstakes.end(), MSG_MORTGAGE_NOT_FOUND);
    graphene_assert(get_head_block_time() > itor->end_time, MSG_MORTGAGE_LOCKED);
    graphene_assert(itor->claimed == false, MSG_MORTGAGE_CLAIMED);

    inline_transfer(_self, itor->account, CORE_ASSET_ID, itor->amount, LOG_CLAIM, strlen(LOG_CLAIM));

    tbstakes.modify(itor, get_trx_sender(), [&](auto &obj) {
        obj.claimed = true;
        obj.claim_time = get_head_block_time();
    });

    if (itor->staking_type == STAKING_TYPE_VOTE) {
        cancelVote(itor->vote_index, itor->staking_to, itor->amount);
    } else if (itor->staking_type == STAKING_TYPE_TO_SUPER) {
        disableSuperStar(itor->staking_to);
    } else if (itor->staking_type == STAKING_TYPE_SELF_ACTIVATE) {
        //TODO impl
    } else {
        graphene_assert(false, MSG_UNKNOWN_CLAIM_REASON);
    }
}

void starplan::upgrade(uint64_t flag)
{
    // 0、防止跨合约调用
    uint64_t sender_id = checkSender();

    // 1、验证合约是否已经初始化
    graphene_assert(isInit(), MSG_NOT_INITIALIZED);

    // 2、验证调用者账户是否为admin账户
    graphene_assert(sender_id == ADMIN_ID, MSG_CHECK_ADMIN);

    // 3、修改global表
    auto itor = tbglobals.find(0);
    tbglobals.modify(itor, sender_id, [&](auto &obj) {
        obj.upgrading = flag;
    });
}

void starplan::updatememo(const std::string &memo)
{
    graphene_assert(memo.size() <= MAX_MEMO_LENGTH, MSG_MEMO_TOO_LONG);
    baseCheck();

    // 1、判断账户是否为超级星
    uint64_t sender_id = get_trx_origin();
    graphene_assert(superstarEnabled(sender_id), MSG_SUPER_STAR_NOT_EXIST);
    // 2、更新账户memo
    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
    auto sup_itor = sup_idx.find(sender_id);
    sup_idx.modify(sup_itor, sender_id, [&](auto &obj) {
        obj.memo = memo;
    });
}

void starplan::getbudget()
{
    baseCheck();
    endRoundCheck(lastRound().bstate.finished == false, MSG_GET_BUDGET);
    calcBudgets();
}

void starplan::calcrdmrwd()//TODO 测试性能
{
    baseCheck();

    const struct tbround &curRound = lastRound();
    bool check = curRound.bstate.finished == true && curRound.rstate.randomPoolReady == false;
    endRoundCheck(check, MSG_PROGRESS_RANDOM_REWARDS);

    vector<uint64_t> bigPlanets;
    vector<uint64_t> bigPlanetsToReward;

    getCurrentRoundBigPlanets(bigPlanets);
    chooseBigPlanet(bigPlanets, bigPlanetsToReward);
    uint64_t sender_id = get_trx_sender();

    uint64_t actualRewardAmount = 0;

    if(bigPlanetsToReward.size() > 0) {
        uint64_t rewardPerPlanet = curRound.bstate.randomBudget / bigPlanetsToReward.size();
        graphene_assert(rewardPerPlanet <= MAX_USER_REWARD, MSG_USER_REWARD_TOO_MUCH);
        for(auto bigPlanetId : bigPlanetsToReward) {
            actualRewardAmount += rewardPerPlanet;
            tbrewards.emplace(sender_id, [&](auto &obj) {
                obj.index   = tbrewards.available_primary_key();
                obj.round   = currentRound();
                obj.from    = sender_id;
                obj.to      = bigPlanetId;
                obj.amount  = rewardPerPlanet;
                obj.type    = RWD_TYPE_RANDOM;
                obj.rewarded = false;
            });
        }
    }

    tbrounds.modify(curRound, sender_id, [&](auto &obj) {
        obj.rstate.randomPoolReady = true;
        obj.actualReward += actualRewardAmount;

        graphene_assert(obj.actualReward <= MAX_ROUND_REWARD + curRound.bstate.randomBudget, MSG_ROUND_REWARD_TOO_MUCH);
    });
}

void starplan::calcbigrwd()
{
    baseCheck();

    const struct tbround &curRound = lastRound();
    bool check = curRound.bstate.finished == true && curRound.rstate.bigReady == false;
    endRoundCheck(check,MSG_PROGRESS_BIG_REWARDS);

    vector<uint64_t> bigPlanets;
    getCurrentRoundBigPlanets(bigPlanets);

    uint64_t sender_id = get_trx_sender();
    if (bigPlanets.size() == 0) {
        tbrounds.modify(lastRound(), sender_id, [](auto &obj) {
            obj.rstate.bigReady = true;
        });
        return;
    }

    uint64_t lastBigPlanet = 0;
    uint64_t actualRewardAmount = 0;


    if(isInviteTimeout(lastBigPlanet)) {// 如果超过12小时没有新的invitee则所有奖励归当轮最后一个大行星
        tbrewards.emplace(sender_id, [&](auto &obj){
            obj.index = tbrewards.available_primary_key();
            obj.round = curRound.round;
            obj.from = _self;
            obj.to = lastBigPlanet;
            obj.amount = curRound.bstate.bigPlanetBudget;
            obj.type = RWD_TYPE_TIMEOUT;
            obj.rewarded = false;
        });
        graphene_assert(curRound.bstate.bigPlanetBudget <= MAX_USER_REWARD, MSG_USER_REWARD_TOO_MUCH);
        actualRewardAmount = curRound.bstate.bigPlanetBudget;
    } else {
        uint64_t rewardPerPlanet = curRound.bstate.bigPlanetBudget / bigPlanets.size();
        graphene_assert(rewardPerPlanet <= MAX_USER_REWARD, MSG_USER_REWARD_TOO_MUCH);
        for(auto bigPlanetId : bigPlanets) {
            actualRewardAmount += rewardPerPlanet;
            tbrewards.emplace(sender_id, [&](auto &obj){
                obj.index = tbrewards.available_primary_key();
                obj.round = curRound.round;
                obj.from = _self;
                obj.to = bigPlanetId;
                obj.amount = rewardPerPlanet;
                obj.type = RWD_TYPE_POOL;
                obj.rewarded = 0;
            });
        }
    }

    tbrounds.modify(curRound, sender_id, [&](auto &obj) {
        obj.rstate.bigReady = true;
        obj.actualReward += actualRewardAmount;

        graphene_assert(obj.actualReward <= MAX_ROUND_REWARD + curRound.bstate.randomBudget, MSG_ROUND_REWARD_TOO_MUCH);
    });
}

void starplan::calcactrwd()
{
    baseCheck();

    const struct tbround &curRound = lastRound();
    auto g_itor = tbglobals.find(0);
    bool check = curRound.bstate.finished == true && curRound.rstate.activeReady == false && g_itor->total_weight != 0;
    endRoundCheck(check, MSG_PROGRESS_ACTIVE_REWARDS);

    uint64_t sender_id = get_trx_sender();

    // 2 计算每个活力星发的奖励
    auto act_idx = tbactiveplans.get_index<N(bytrave)>();
    auto id = curRound.rstate.traveIndex | 0x0100000000000000;
    auto itor = act_idx.lower_bound(id);
    uint64_t amount = 0;
    uint64_t totalAmount = 0;
    for(uint64_t count = 0;itor != act_idx.end() && itor->trave_index > 0x0100000000000000; ){
        if(count >= COUNT_OF_TRAVERSAL_PER) {
            tbrounds.modify(curRound, sender_id, [&](auto &obj) {
                obj.actualReward                    +=  totalAmount;
                obj.rstate.traveIndex               =   itor->trave_index;
            });
            return;
        }else{
            amount = curRound.bstate.superStarBudget * itor->weight /  g_itor->total_weight;
            graphene_assert(amount <= MAX_USER_REWARD, MSG_USER_REWARD_TOO_MUCH);
            totalAmount += amount;
            tbrewards.emplace(sender_id, [&](auto &obj){
                obj.index = tbrewards.available_primary_key();
                obj.round = currentRound();
                obj.from = _self;
                obj.to = itor->id;
                obj.amount = amount;
                obj.type = RWD_TYPE_ACTIVE;
                obj.rewarded = 0;
            });
            auto pri_itor = tbactiveplans.find(itor->index);
            graphene_assert(pri_itor != tbactiveplans.end(), MSG_ACTIVE_PLANET_NOT_FOUND);
            itor++,count++;
            tbactiveplans.modify(pri_itor, get_trx_sender(), [&](auto &obj){                           //修改活力星的权重
                uint64_t new_weight  = obj.weight * B_DECAY_PERCENT / 100;
                obj.weight = new_weight;
                if(obj.weight == 0) obj.trave_index = obj.trave_index & 0xF0FFFFFFFFFFFFFF;
            });
        }
    }

    tbrounds.modify(curRound, sender_id, [&](auto &obj) {
        obj.rstate.activeReady = true;
        obj.actualReward += totalAmount;

        graphene_assert(obj.actualReward <= MAX_ROUND_REWARD + curRound.bstate.randomBudget, MSG_ROUND_REWARD_TOO_MUCH);
    });
}

void starplan::calcactrwd1()//全表遍历 假设10ms可以遍历200条，1秒钟可以遍历20000条，这个性能可以接受
{
	//TODO FIXME round表遍历完之后不修改round状态
    baseCheck();

    const struct tbround &curRound = lastRound();
    auto g_itor = tbglobals.find(0);
    bool check = curRound.bstate.finished == true &&
                 curRound.rstate.activeReady == false &&
                 g_itor->total_weight != 0;
    endRoundCheck(check, MSG_PROGRESS_ACTIVE_REWARDS);

    uint64_t sender_id = get_trx_sender();

    uint64_t count = 0;
    uint64_t amount = 0;
    uint64_t totalAmount = 0;
    auto itor = tbactiveplans.find(curRound.rstate.traveIndex);
    print("curRound.rstate.traveIndex=", curRound.rstate.traveIndex, "\n");
    print("itor->weight=", itor->weight, "\n");
    do {
        if(itor->weight > 0) {
            amount = curRound.bstate.superStarBudget * itor->weight / g_itor->total_weight;
            graphene_assert(amount <= MAX_USER_REWARD, MSG_USER_REWARD_TOO_MUCH);
            totalAmount += amount;

            tbrewards.emplace(sender_id, [&](auto &obj) {
                obj.index       = tbrewards.available_primary_key();
                obj.round       = curRound.round;
                obj.from        = _self;
                obj.to          = itor->id;
                obj.amount      = amount;
                obj.type        = RWD_TYPE_ACTIVE;
                obj.rewarded    = 0;
            });

            tbactiveplans.modify(itor, sender_id, [](auto &obj) {
                obj.weight = obj.weight * B_DECAY_PERCENT / 100;
            });
        }

        count++;
        if(count == COUNT_OF_TRAVERSAL_PER) break;
        print("count=", count, "\n");

        itor++;
        if(itor == tbactiveplans.end()) {
        	print("A\n");
            tbrounds.modify(curRound, sender_id, [&](auto &obj) {
                obj.rstate.activeReady = true;
                obj.rstate.traveIndex += count;
                obj.actualReward += totalAmount;
                graphene_assert(obj.actualReward <= MAX_ROUND_REWARD + curRound.bstate.randomBudget, MSG_ROUND_REWARD_TOO_MUCH);
            });

            return;
        }
    } while (true);

    print("B\n");
    tbrounds.modify(curRound, sender_id, [&](auto &obj) {
    	print("C\n");
        obj.actualReward += totalAmount;
        obj.rstate.traveIndex += COUNT_OF_TRAVERSAL_PER;
        graphene_assert(obj.actualReward <= MAX_ROUND_REWARD + curRound.bstate.randomBudget, MSG_ROUND_REWARD_TOO_MUCH);
    });
}

void starplan::calcsuprwd()
{
    baseCheck();

    const struct tbround &curRound = lastRound();
    bool check = curRound.bstate.finished == true && curRound.rstate.superReady == false;
    endRoundCheck(check, MSG_PROGRESS_SUPER_REWARDS);

    uint64_t sender_id = get_trx_sender();

    vector<SuperStar> superStars;
    uint64_t totalVote = getCurrentRoundSuperStars(superStars);
    if (totalVote == 0) {
        tbrounds.modify(curRound, sender_id, [&](auto &obj) {
            obj.rstate.superReady = true;
        });

        return;
    }

    uint64_t amount = 0;
    uint64_t totalAmount = 0;
    for(const auto &superStar : superStars) {
        if(superStar.vote <= 0) continue;
        amount = curRound.bstate.superStarBudget * superStar.vote / totalVote;
        graphene_assert(amount <= MAX_USER_REWARD, MSG_USER_REWARD_TOO_MUCH);
        totalAmount += amount;
        tbrewards.emplace(sender_id, [&](auto &obj){
            obj.index = tbrewards.available_primary_key();
            obj.round = curRound.round;
            obj.from = _self;
            obj.to = superStar.id;
            obj.amount = amount;
            obj.type = RWD_TYPE_SUPER;
            obj.rewarded = 0;
        });
    }

    tbrounds.modify(curRound, sender_id, [&](auto &obj) {
        obj.rstate.superReady = true;
        obj.actualReward += totalAmount;

        graphene_assert(obj.actualReward <= MAX_ROUND_REWARD + curRound.bstate.randomBudget, MSG_ROUND_REWARD_TOO_MUCH);
    });
}

void starplan::dorwd(uint64_t limit)
{
    baseCheck();

    const struct tbround &curRound = lastRound();//TODO FIXME 当上一轮的奖励还美发，又开始下一轮之后，之前的奖励发送不出去，报Current round is not end
    bool check = curRound.bstate.finished == true &&
                 curRound.rstate.bigReady == true &&
                 curRound.rstate.randomPoolReady == true &&
                 curRound.rstate.activeReady == true &&
                 curRound.rstate.superReady == true;

    endRoundCheck(check, MSG_CALC_REWARDS);

    auto rwd_idx = tbrewards.get_index<N(byflag)>();
    auto itor = rwd_idx.find(0);
    graphene_assert(itor != rwd_idx.end(), MSG_REWARDS_NOT_FOUND);

    for (auto i = 0; i < limit; i++) {
        if (itor->amount == 0) continue;
//        if (get_head_block_time() - itor->createTime <= 60) continue;//TODO 防止之前的交易被回滚，等到reward记录不可逆之后再发奖励
        if (itor == rwd_idx.end() || itor->rewarded == true) break;

        inline_transfer(_self,
                        itor->to,
                        CORE_ASSET_ID,
                        itor->amount,
                        reward_reasons[itor->type],
                        strlen(reward_reasons[itor->type])
        );

        auto pri_itor = tbrewards.find(itor->index);
        itor++;
        tbrewards.modify(pri_itor, get_trx_sender(), [](auto &obj) {
            obj.rewarded = 1;
        });
    }
}

void starplan::newround()
{
    baseCheck();

    const starplan::tbround &curRound = lastRound();

    bool check = curRound.bstate.finished == true &&
            curRound.rstate.bigReady == true &&
            curRound.rstate.randomPoolReady == true &&
            curRound.rstate.activeReady == true &&
            curRound.rstate.superReady == true;

    endRoundCheck(check, MSG_CALC_REWARDS);

    uint64_t sender_id = get_trx_sender();

     // 1、修改总的资金池
    auto g_itor = tbglobals.find(0);
    tbglobals.modify(g_itor, sender_id, [&](auto &obj) {
        obj.pool_amount -= curRound.actualReward;//TODO check overflow
        obj.total_weight = obj.total_weight * B_DECAY_PERCENT / 100;
    });

    tbrounds.modify(curRound, sender_id, [&](auto &obj) {
        obj.actual_rewards = curRound.actualReward;
    });

    createNewRound();
}

bool starplan::isInit()
{
    auto global_itor = tbglobals.find(0);
    auto round_itor = tbrounds.find(0);
    return global_itor != tbglobals.end() && round_itor != tbrounds.end();
}

bool starplan::isInviter(std::string accname)
{
    uint64_t inviter_id = get_account_id(accname.c_str(), accname.length());
    return superstarEnabled(inviter_id) || isBigPlanet(inviter_id);
}

bool starplan::superstarEnabled(uint64_t superId)
{
    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
    auto sup_itor = sup_idx.find(superId);
    return sup_itor != sup_idx.end() && sup_itor->disabled == false;
}

bool starplan::superstarExist(uint64_t superId)
{
    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
    auto sup_itor = sup_idx.find(superId);
    return sup_itor != sup_idx.end();
}

void starplan::createSuperstar(uint64_t accountId, const std::string &memo)
{
    tbsuperstars.emplace(accountId, [&](auto &obj) {
        obj.index                   = tbsuperstars.available_primary_key();
        obj.id                      = accountId;
        obj.create_time             = get_head_block_time();
        obj.create_round            = currentRound();
        obj.vote_num                = 0;
        obj.disabled                = false;
        obj.memo                    = memo;
    });
}

void starplan::enableSuperstar(uint64_t superId, const std::string &memo) {
    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
    auto sup_itor = sup_idx.find(superId);
    sup_idx.modify(sup_itor, superId, [&](auto &obj) {
        obj.disabled                = false;
        obj.memo                    = memo;
        obj.create_time             = get_head_block_time();
        obj.create_round            = currentRound();
//        obj.vote_num                = 0;//TODO CHECK
    });
}

bool starplan::isSmallPlanet(uint64_t sender)
{
    auto small_idx = tbsmallplans.get_index<N(byaccid)>();
    auto small_itor = small_idx.find(sender);
    return small_itor != small_idx.end();
}

void starplan::createSmallPlanet(uint64_t sender)
{
    tbsmallplans.emplace(sender, [&](auto &obj) {
        obj.index           = tbsmallplans.available_primary_key();
        obj.id              = sender;
        obj.create_time     = get_head_block_time();
        obj.create_round    = currentRound();
    });
}

bool starplan::isBigPlanet(uint64_t sender)
{
    bool retValue = false;
    auto big_idx = tbbigplanets.get_index<N(byaccid)>();
    auto big_itor = big_idx.find(sender);
    if (big_itor != big_idx.end()) {
        retValue = true;
    }
    return retValue;
}

void starplan::createBigPlanet(uint64_t sender)
{
    tbbigplanets.emplace(sender, [&](auto &obj) {
        obj.index = tbbigplanets.available_primary_key();
        obj.id = sender;
        obj.create_time = get_head_block_time();
        obj.create_round = currentRound();
    });
}

bool starplan::hasInvited(uint64_t invitee)
{
    auto invitee_idx = tbinvites.get_index<N(byinvitee)>();
    auto invite_itor = invitee_idx.find(invitee);
    return invite_itor != invitee_idx.end();
}

bool starplan::isInviteTimeout(uint64_t &lastBigPlanet)
{
    auto big_itor = tbbigplanets.end();
    if (big_itor == tbbigplanets.begin()) {
        return false;
    }
    big_itor--;

    if (big_itor->create_round != currentRound()) {
        return false;
    }

    graphene_assert(get_head_block_time() > big_itor->create_time, MSG_ILLIGAL_BIG_PLANET_CREATE_TIME);
    if ((get_head_block_time() - big_itor->create_time) > DELAY_TIME) {
        lastBigPlanet = big_itor->id;
        return true;
    }

    return false;
}

bool starplan::isRoundFull()
{
    auto round_itor = tbrounds.end();
    graphene_assert(round_itor != tbrounds.begin(), MSG_ROUND_NOT_FOUND);
    round_itor--;
    return round_itor->current_round_invites >= ROUND_SIZE;
}

bool starplan::isRoundFinish()
{
    uint64_t lastBigPlanet = 0;
    return isInviteTimeout(lastBigPlanet) || isRoundFull();
}

uint64_t starplan::currentRound()
{
    auto itor = tbglobals.find(0);
    return itor->current_round;
}

void starplan::invite(uint64_t invitee, uint64_t inviter)
{
    if (!hasInvited(invitee)) {
        tbinvites.emplace(invitee, [&](auto &obj) {
            obj.index           = tbinvites.available_primary_key();
            obj.invitee         = invitee;
            obj.inviter         = inviter;
            obj.enabled         = false;
            obj.create_round    = currentRound();
            obj.create_time     = get_head_block_time();
        });
    }
}

void starplan::activateInvite(uint64_t sender)
{
    auto invite_idx = tbinvites.get_index<N(byinvitee)>();
    auto invite_itor = invite_idx.find(sender);
    if(invite_itor != invite_idx.end()) {
        invite_idx.modify(invite_itor, sender, [&](auto &obj) {
            obj.enabled = true;
        });
    }
}

void starplan::progress(uint64_t ramPayer)
{
    tbrounds.modify(lastRound(), ramPayer, [&](auto &obj) {
        obj.current_round_invites = obj.current_round_invites + 1;
    });
}

uint64_t starplan::createVote(uint64_t sender, uint64_t super_id, uint64_t voteCount)
{
    uint64_t vote_id;
    tbvotes.emplace(sender,[&](auto &obj) {
        obj.index                   = tbvotes.available_primary_key();
        obj.round                   = currentRound();
        obj.staking_amount          = voteCount;
        obj.from                    = sender;
        obj.to                      = super_id;
        obj.vote_time               = get_head_block_time();
        obj.disabled                = false;
        vote_id                     = obj.index;
    });

    return vote_id;
}

void starplan::updateSuperstarVote(uint64_t account, uint64_t voteCount, uint64_t feePayer)
{
    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
    auto sup_itor = sup_idx.find(account);
    if (sup_itor != sup_idx.end()) {
        sup_idx.modify(sup_itor, feePayer, [&voteCount](auto &obj) {
            obj.vote_num += voteCount;
        });
    }
}

void starplan::createStaking(uint64_t sender, uint64_t amount, uint64_t to, uint64_t stakingType, uint64_t index)
{
    tbstakes.emplace(sender, [&](auto &obj) {
        obj.index                   = tbstakes.available_primary_key();
        obj.account                 = sender;
        obj.amount                  = amount;
        obj.end_time                = get_head_block_time() + STAKING_DURATION_TIME;
        obj.staking_to              = to;
        obj.staking_type            = stakingType;
        obj.claimed                 = false;
        obj.claim_time              = 0;
        obj.vote_index              = index;
    });
}

uint64_t starplan::getInviter(uint64_t invitee)
{
    auto invite_idx = tbinvites.get_index<N(byinvitee)>();
    auto invite_itor = invite_idx.find(invitee);
    graphene_assert(invite_itor != invite_idx.end(), MSG_INVITER_NOT_EXIST);
    return invite_itor->inviter;
}

void starplan::buildRewardReason(uint64_t invitee, uint64_t inviter, uint64_t rewardType, std::string &rewardReason)
{
    char inviteeName[64] = { 0 };
    char inviterName[64] = { 0 };

    if(RWD_TYPE_SELF_ACTIVATE == rewardType) {
        graphene_assert(0 == get_account_name_by_id(inviteeName, 63, invitee), MSG_GET_INVITEE_NAME_FAIL);
        rewardReason = std::string(inviterName) + " get reward for self activate";
    } else if (RWD_TYPE_INVITE == rewardType) {
        graphene_assert(0 == get_account_name_by_id(inviteeName, 63, invitee), MSG_GET_INVITEE_NAME_FAIL);
        graphene_assert(0 == get_account_name_by_id(inviterName, 63, inviter), MSG_GET_INVITER_NAME_FAIL);
        rewardReason = std::string(inviterName) + " get reward for inviting " + std::string(inviteeName);
    } else {
    }
} 

void starplan::buildDepositMsg(uint64_t amount, bool equalCheck, std::string &msg)
{
    if (equalCheck)
        msg = "Error: " + std::to_string(amount / PRECISION) + " GXC required";
    else
        msg = "Error: Minimum " + std::to_string(amount / PRECISION) + " GXC required";
}

void starplan::distributeInviteRewards(uint64_t invitee, uint64_t rewardAccountId, uint64_t rewardType)
{
    std::string rewardReason;
    buildRewardReason(invitee, rewardAccountId, rewardType, rewardReason);
    inline_transfer(_self, rewardAccountId, CORE_ASSET_ID, Z2, rewardReason.c_str(), rewardReason.length());

    tbrounds.modify(lastRound(), invitee, [&](auto &obj)
    {
        obj.random_pool_amount = obj.random_pool_amount + Z3;
        obj.invite_reward_amount = obj.invite_reward_amount + Z1;
    });

    tbrewards.emplace(invitee, [&](auto &obj)
    {
        obj.index = tbrewards.available_primary_key();
        obj.round = currentRound();
        obj.from = invitee;
        obj.to = rewardAccountId;
        obj.amount = Z2;
        obj.type = rewardType;
        obj.rewarded = true;
    });
}

void starplan::updateActivePlanet(uint64_t activePlanetAccountId, uint64_t inviteeId)
{
    auto act_idx = tbactiveplans.get_index<N(byaccid)>();
    auto act_itor = act_idx.find(activePlanetAccountId);
    bool is_add_weight = false;
    if (act_itor != act_idx.end()) {
        act_idx.modify(act_itor, inviteeId, [&](auto &obj) {
            obj.invitees.push_back(inviteeId);
            if(obj.invitees.size() == ACTIVE_PROMOT_INVITES) {
                obj.weight += WEIGHT;
                is_add_weight = true;
                obj.invitees.clear();
                obj.trave_index = obj.trave_index | 0x0100000000000000;
            }
        });
    } else {
        tbactiveplans.emplace(inviteeId, [&](auto &obj) {                                      //创建活力星
            obj.index = tbactiveplans.available_primary_key();
            obj.id = activePlanetAccountId;
            obj.invitees.push_back(inviteeId);
            obj.create_time = get_head_block_time();
            obj.create_round = currentRound();
            obj.weight = 0;
            obj.trave_index = activePlanetAccountId | 0x0000000000000000;
        });
    }
    if(is_add_weight == true)                                                                    //更新全局权重
    {
        auto g_itor = tbglobals.find(0);
        tbglobals.modify(g_itor,inviteeId,[&](auto &obj){
            obj.total_weight      = obj.total_weight + WEIGHT;
        });
    }
}

void starplan::updateActivePlanetForSuper(uint64_t activePlanetAccountId)
{
    auto act_idx = tbactiveplans.get_index<N(byaccid)>();
    auto act_itor = act_idx.find(activePlanetAccountId);
    if (act_itor != act_idx.end()) {
        act_idx.modify(act_itor, activePlanetAccountId, [&](auto &obj) {
            obj.weight += WEIGHT;
            obj.trave_index = obj.trave_index | 0x0100000000000000;
        });
    } else {
        tbactiveplans.emplace(activePlanetAccountId, [&](auto &obj) {                                      //创建活力星
            obj.index = tbactiveplans.available_primary_key();
            obj.id = activePlanetAccountId;
            obj.invitees = {};
            obj.create_time = get_head_block_time();
            obj.create_round = currentRound();
            obj.weight = WEIGHT;
            obj.trave_index = activePlanetAccountId | 0x0100000000000000;
        });
    }
    auto g_itor = tbglobals.find(0);
    tbglobals.modify(g_itor,activePlanetAccountId, [&](auto &obj){
        obj.total_weight += WEIGHT;
    });
}

void starplan::calcBudgets()
{
    auto &round = lastRound();

    auto pool_amount = ROUND_AMOUNT + round.invite_reward_amount;

    uint64_t decayAmountUnit = (currentRound() % BIG_ROUND_SIZE + 1) * PRECISION;//TODO check 第一轮衰减吗？

    uint64_t now = (uint64_t)get_head_block_time();
    graphene_assert(now > round.start_time, MSG_BLOCK_TIME_ERR);
    uint64_t curRoundElapseTime = now - round.start_time;

    uint64_t decayCount = (curRoundElapseTime - DECAY_TIME) / DECAY_DURATION;
    decayCount += (curRoundElapseTime - DECAY_TIME) % DECAY_DURATION > 0 ? 1 : 0;
    decayCount = decayCount > MAX_DECAY_COUNT ? MAX_DECAY_COUNT : decayCount;

    uint64_t decayAmount = decayAmountUnit * decayCount;
    graphene_assert(pool_amount > decayAmount, MSG_POOL_AMOUNT_DECAY_ERR);
    pool_amount = pool_amount - decayAmount;

    uint64_t randomBudget = round.random_pool_amount;
    uint64_t bigPlanetBudget = pool_amount * PAYBACK_PERCENT / 100;
    uint64_t activePlanetBudget = pool_amount * ACTIVE_PERCENT / 100;
    uint64_t superStarBudget = pool_amount - bigPlanetBudget - activePlanetBudget;

    auto sender = get_trx_sender();
    tbrounds.modify(round, sender, [&](auto &obj){
        obj.base_pool_amount            = pool_amount;
        obj.bstate.randomBudget         = randomBudget;
        obj.bstate.bigPlanetBudget      = bigPlanetBudget;
        obj.bstate.activePlanetBudget   = activePlanetBudget;
        obj.bstate.superStarBudget      = superStarBudget;
        obj.bstate.finished                 = true;
    });
}

void starplan::getCurrentRoundBigPlanets(vector<uint64_t> &bigPlanets)
{
    auto big_idx = tbbigplanets.get_index<N(byround)>();
    auto round = currentRound();
    auto itor = big_idx.find(round);
    while (itor != big_idx.end() && itor->create_round == round)
    {
        bigPlanets.push_back(itor->id);
        itor++;
    }
}

uint64_t starplan::getCurrentRoundSuperStars(vector<SuperStar> &superStars)
{
    uint64_t total_votes = 0;

    for (auto itor = tbsuperstars.begin(); itor != tbsuperstars.end(); itor++)
    {
        if(itor->disabled == false){
            total_votes += itor->vote_num;
            superStars.push_back(SuperStar{itor->id, itor->vote_num, itor->vote_num != 0});
        }
    }

    return total_votes;
}

void starplan::chooseBigPlanet(const vector<uint64_t> &bigPlanets, vector<uint64_t> &choosed)
{
    if(bigPlanets.size() <= RANDOM_COUNT){
        choosed = bigPlanets;
        return;
    }
    auto bigplanet_size = RANDOM_COUNT;
    int64_t block_num = get_head_block_num();
    uint64_t block_time = get_head_block_time();
    std::string random_str = std::to_string(block_num) + std::to_string(block_time);
    checksum160 sum160;
    ripemd160(const_cast<char *>(random_str.c_str()), random_str.length(), &sum160);
    checksum160 block_hash;
    get_head_block_id(&block_hash);
    for (uint64_t i = 0; i < bigplanet_size; i++)
    {
        auto j = i;
        while (true)
        {
            uint8_t share = (uint8_t) (sum160.hash[j % 20] * (j + 1));
            uint8_t number = (share + block_hash.hash[j % 20]) % bigplanet_size;
            auto it = std::find(choosed.begin(), choosed.end(), number);
            if (it != choosed.end())
            {
                j++;
                continue;
            }
            else
            {
                choosed.push_back(bigPlanets[number]);
                break;
            }
        }
    }
}

const struct starplan::tbround& starplan::lastRound()
{
    auto round_itor = tbrounds.end();
    graphene_assert(round_itor != tbrounds.begin(), MSG_ROUND_NOT_FOUND);
    round_itor--;
    return *round_itor;
}

void starplan::createNewRound()
{
    // 1 结束当前轮，修改round表和global表
    uint64_t sender = get_trx_sender();
    tbrounds.modify(lastRound(), sender, [&](auto &obj) {
        obj.end_time = get_head_block_time();
    });

    // 2 创建新的一轮
    auto g_itor = tbglobals.find(0);
    tbglobals.modify(g_itor,sender,[&](auto &obj){
        obj.current_round += 1;
    });

    tbrounds.emplace(sender,[&](auto &obj) {
        obj.round                   = tbrounds.available_primary_key();
        obj.current_round_invites   = 0;
        obj.actual_rewards          = 0;
        obj.base_pool_amount        = 0;
        obj.random_pool_amount      = 0;
        obj.invite_reward_amount    = 0;
        obj.start_time              = get_head_block_time();
        obj.end_time                = 0;
    });
}

uint64_t starplan::checkSender()
{
    auto sender_id = get_trx_sender();
    auto origin_id = get_trx_origin();
    graphene_assert(sender_id == origin_id, MSG_CHECK_SENDER);
    return sender_id;
}

bool starplan::isUpgrading()
{
    auto itor = tbglobals.find(0);
    return itor->upgrading > 0;
}

void starplan::cancelVote(uint64_t voteIndex, uint64_t superAccId, uint64_t amount)
{
    // 修改投票表
    auto vote_itor = tbvotes.find(voteIndex);
    graphene_assert(vote_itor != tbvotes.end(), MSG_INVALID_ITOR);
    tbvotes.modify(vote_itor, get_trx_sender(), [&](auto &obj) {
        obj.disabled = true;//TODO check 修改了也没用了，只是用来记录和展示
    });

    // 修改超级星得票数
    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
    auto sup_itor = sup_idx.find(superAccId);
    graphene_assert(sup_itor != sup_idx.end(), MSG_INVALID_ITOR);
    sup_idx.modify(sup_itor, get_trx_sender(), [&](auto &obj) {
        graphene_assert(obj.vote_num >= amount, MSG_INVALID_VOTE_MODIFY);
        obj.vote_num -= amount;//TODO check overflow
    });
}

void starplan::disableSuperStar(uint64_t superId)
{
    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
    auto sup_itor = sup_idx.find(superId);
    graphene_assert(sup_itor != sup_idx.end(), MSG_SUPER_STAR_NOT_EXIST);
    sup_idx.modify(sup_itor, get_trx_sender(), [&](auto &obj) {
        obj.disabled = true;
    });
}

void starplan::baseCheck()
{
    checkSender();
    graphene_assert(isInit(), MSG_NOT_INITIALIZED);
    graphene_assert(!isUpgrading(), MSG_UPGRADING);
}

void starplan::roundFinishCheck()
{
    graphene_assert(!isRoundFinish(), MSG_ROUND_END);
}

uint64_t starplan::assetEqualCheck(uint64_t expectedAmount)
{
    std::string errMsg;
    buildDepositMsg(expectedAmount, true, errMsg);
    graphene_assert(get_action_asset_id() == CORE_ASSET_ID, MSG_CORE_ASSET_REQUIRED);
    graphene_assert(get_action_asset_amount() == expectedAmount, errMsg.c_str());

    return expectedAmount;
}

uint64_t starplan::assetLargerCheck(uint64_t expectedAmount)
{
    std::string errMsg;
    buildDepositMsg(expectedAmount, false, errMsg);
    uint64_t actualAmount = get_action_asset_amount();
    graphene_assert(get_action_asset_id() == CORE_ASSET_ID, MSG_CORE_ASSET_REQUIRED);
    graphene_assert(actualAmount >= expectedAmount, errMsg.c_str());

    return actualAmount;
}

uint64_t starplan::inviterCheck(const std::string &inviter, uint64_t inviteeId)
{
    if ("" != inviter) {
        uint64_t inviter_id = get_account_id(inviter.c_str(), inviter.length());
        graphene_assert(isInviter(inviter), MSG_CHECK_INVITER_VALID);
        graphene_assert(inviter_id != inviteeId, MSG_CHECK_INVITE_SELF);
        return inviter_id;
    }

    return DEFAULT_INVITER;
}

uint64_t starplan::superStarCheck(const std::string &superStarAccount)
{
    int64_t super_id = get_account_id(superStarAccount.c_str(), superStarAccount.length());
    graphene_assert(superstarEnabled(super_id), MSG_SUPER_STAR_NOT_EXIST);//TODO FIXME check super_id type
    return super_id;
}

uint64_t starplan::updateAccountVote(uint64_t sender, uint64_t voteCount)
{
    uint64_t totalVotes = 0;

    auto itor = tbaccounts.find(sender);
    if (itor == tbaccounts.end()) {
        tbaccounts.emplace(sender, [&](auto &obj) {
            obj.account_id  = sender;
            obj.vote_count  = voteCount;
            totalVotes      = obj.vote_count;
        });
    } else {
        tbaccounts.modify(itor, sender, [&](auto &obj) {
            obj.vote_count += voteCount;
            totalVotes = obj.vote_count;
        });
    }

    return totalVotes;
}

void starplan::endRoundCheck(bool check, const std::string &msg)
{
    graphene_assert(isRoundFinish(), MSG_ROUND_NOT_END);
    uint64_t sender_id = get_trx_origin();
    graphene_assert(sender_id == ADMIN_ID, MSG_CHECK_ADMIN);
    graphene_assert(check, msg.c_str());
}
