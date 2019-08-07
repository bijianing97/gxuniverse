#include "starplan.hpp"

void starplan::init()
{
    //////////////////////////////////////// 对调用进行校验 /////////////////////////////////////////////////
    // 1 校验调用者，只有调用者可以初始化底池，只许调用一次
    uint64_t sender_id = get_trx_origin();
    graphene_assert(sender_id == adminId, "StarPlan Contract Error: Only admin account can init! ");

    // 2、校验充值的资产是否为2000000GXC
    uint64_t ast_id = get_action_asset_id();
    uint64_t amount = get_action_asset_amount();
    //graphene_assert(ast_id == coreAsset && amount == initPool * precision, "StarPlan Contract Error: Init amount must is 2000000 GXC and Only can deposit GXC asset!");

    // 3、校验底池是否已经初始化
    auto glo_itor = tbglobals.find(0);
    graphene_assert(glo_itor == tbglobals.end(),"StarPlan Contract Error:  The bonus pool has been initialized");
    
    // 4、校验当前轮资金池是否已经初始化
    auto rou_itor = tbrounds.find(0);
    graphene_assert(rou_itor == tbrounds.end(),"StarPlan Contract Error:  The Current bonus pool has been initialized");

    //////////////////////////////////////// 校验通过后，初始化资金池 //////////////////////////////////////////
    // 1、初始化总资金池
    tbglobals.emplace(_self,[&](auto &obj) {
            obj.index           = 0;
            obj.pool_amount     = amount;
            obj.current_round   = 0;
        });
    // 2、初始化第一轮资金池，并启动第一轮
    tbrounds.emplace(_self,[&](auto &obj) {
            obj.round                   = tbrounds.available_primary_key();
            obj.current_round_invites   = 0;       
            obj.pool_amount             = 0;
            obj.random_pool_amount      = 0;
            obj.invite_pool_amount      = 0;
            obj.start_time              = get_head_block_time();;
            obj.end_time                = 0;
        });
}
void starplan::uptosmall(std::string inviter,std::string superstar)
{
    //////////////////////////////////////// 对调用进行校验 /////////////////////////////////////////////////
    uint64_t ast_id = get_action_asset_id();
    uint64_t amount = get_action_asset_amount();

    //1、判断是否存入足够GXC
    //graphene_assert(ast_id == coreAsset && amount == y * precision, "100 GXC required");
    
    uint64_t sender_id = get_trx_origin();
    //2、验证账户
    if(inviter != ""){
        graphene_assert(isAccount(inviter), "inviter account is invalid");
        //3、验证邀请账户
        uint64_t inviter_id = get_account_id(inviter.c_str(), inviter.length());
        graphene_assert(isInviter(inviter), "StarPlan Contract Error: Inviters must be big planets and super star"); 
        graphene_assert(inviter_id != sender_id, "StarPlan Contract Error: Can't invite yourself ");
        
    }
    graphene_assert(isAccount(superstar), "superStar account is invalid");

    //4、验证当前轮是否结束
    graphene_assert(!bSmallRound(),"Current round is end, can't create small planet");

    //5、验证超级星账户
    auto super_id = get_account_id(superstar.c_str(), superstar.length());
    graphene_assert(isSuperStar(super_id), "StarPlan Contract Error: No found super planet account in superplanets table");

    //6、检查global表和round表的状态
    graphene_assert(isInit(), "Please initialize the game first");

    //////////////////////////////////////// 校验通过后，创建一个小行星 //////////////////////////////////////////
    //7、存到smallPlanet表(不允许重复创建)
    if(canupdatesmall(sender_id))
        addSmallPlanet(sender_id);

    //8、保存邀请关系(不允许重复邀请)
    invite(sender_id,inviter);
    
    //9、vote(允许重复投票)
    vote(sender_id,superstar);

    //10、添加一个新的抵押金额
    addStake(sender_id,amount,super_id,vote_reason);

    //11、修改超级星的得票数
    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
    auto sup_itor = sup_idx.find(super_id);
    sup_idx.modify(sup_itor,_self,[&](auto &obj) {
            obj.vote_num  = obj.vote_num + amount;
        });
}

void starplan::uptobig()
{
    //////////////////////////////////////// 对调用进行校验 /////////////////////////////////////////////////
    uint64_t ast_id = get_action_asset_id();
    uint64_t amount = get_action_asset_amount();

    //1、判断是否存入足够GXC
    graphene_assert(ast_id == coreAsset && amount == depositToBig * precision, "3 GXC required");

    //2、判断是否是small planet，如果还不不是，则提示“You have to become a small planet first”
    uint64_t sender_id = get_trx_origin();
    graphene_assert(isSmallPlanet(sender_id), "You have to become a small planet first");

    //3、判断是否已经是bigPlanet，如果已经是，则提示"You are already a big planet"
    graphene_assert(!isBigPlanet(sender_id), "You are already a big planet");

    //4、验证当前轮是否结束
    graphene_assert(!bSmallRound(),"Current round is end, can't create big planet");

    //5、验证当前轮状态和global状态
    graphene_assert(isInit(), "Please initialize the game first");

    //////////////////////////////////////// 校验通过后，创建一个大行星 //////////////////////////////////////////
    //6、存到bigPlanet表
    addBigPlanet(sender_id);

    //7、激活邀请关系
    actinvite(sender_id);

    //8、将3个GXC转移到奖池，将其中一个GXC发送给邀请人
    sendInviteReward(sender_id);

    //9、创建/更新活力星
    updateActivePlanetsbybig(sender_id);
}

// inviter为0，表示没有邀请账户
void starplan::uptosuper(std::string inviter)
{
    //////////////////////////////////////// 对调用进行校验 /////////////////////////////////////////////////
    uint64_t ast_id = get_action_asset_id();
    uint64_t amount = get_action_asset_amount();

    //1、判断是否存入足够GXC
    //graphene_assert(ast_id == coreAsset && amount == x * precision, "20000 GXC required");

    //2、判断是否已经是superstar，如果已经是，则提示"You are already a super star"
    uint64_t sender_id = get_trx_origin();
    graphene_assert(!isSuperStar(sender_id), "You are already a super star");

    //3、验证账户是否存在
    if(inviter != ""){
        graphene_assert(isAccount(inviter), "inviter account is invalid");
        // 4、验证邀请账户
        graphene_assert(isInviter(inviter), "StarPlan Contract Error: Inviters must be big planets and super star"); 
        uint64_t inviter_id = get_account_id(inviter.c_str(), inviter.length());
        graphene_assert(inviter_id != sender_id, "StarPlan Contract Error: Can't invite yourself ");
    }
    //5、验证当前轮是否结束
    graphene_assert(!bSmallRound(),"Current round is end, can't create super star");

    //6、验证是否已经初始化
    graphene_assert(isInit(), "Please initialize the game first");

    //////////////////////////////////////// 校验通过后，创建一个超级星 //////////////////////////////////////////

    //7、创建超级星
    addSuperStar(sender_id);
    
    //8、创建抵押项
    addStake(sender_id, amount, sender_id, stake_reason);
    
    //9、保存邀请关系，激活邀请关系
    invite(sender_id,inviter);
    actinvite(sender_id);

    //10、插入更新一条活力星记录，权重为1
    updateActivePlanetsbysuper(sender_id);
}
void starplan::endround()
{
    // 1 验证调用者账户是否为admin账户
    uint64_t sender_id = get_trx_origin();
    graphene_assert(sender_id == adminId, "StarPlan Contract Error: Only support admin account! ");
    // 2 验证当前轮是否可以结束
    graphene_assert(bSmallRound(),"Current round is not end");
    // 3 计算奖池
    calcCurrentRoundPoolAmount();  
    // 4 更新活力星权重
    updateActivePlanets();
    // 5 发放随机奖池奖励
    randomReward();
    // 6 发放当轮晋升的大行星奖励
    rewardBigPlanet();
    // 7 发放活力星奖励
    rewardActivePlanet();
    // 8 发放超级星奖励
    rewardSuperStar();
    // 9 开启新的一轮
    createnewround();
}
void starplan::unstake(std::string account)
{
    const std::string unstake_withdraw = "unstake withdraw";                    //抵押提现
    uint64_t acc_id = get_account_id(account.c_str(), account.length());
    auto sta_idx = tbstakes.get_index<N(byaccid)>();
    auto itor = sta_idx.find(acc_id);
    for(; itor != sta_idx.end() && itor->account == acc_id;){
        if(get_head_block_time() > itor->end_time){
            auto itor_bak = itor;
            // 1 获取抵押原因
            if(itor->reason == vote_reason){
                // 1.1 判断超级星是否还存在，存在则修改超级星得票数
                if(isSuperStar(itor->staketo)){
                    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
                    auto sup_itor = sup_idx.find(itor->staketo);
                    sup_idx.modify(sup_itor,_self,[&](auto &obj) {
                            graphene_assert(obj.vote_num >itor->amount, "StarPlan Contract Error: stake amount is error ! ");
                            obj.vote_num  = obj.vote_num - itor->amount;
                        });
                }
                // 1.2 从vote表中删除该次投票
                deletevote(itor->account,itor->end_time -delayDay );
                
            }else if(itor->reason == stake_reason){
                // 1.2 判断超级星是否还存在，存在则删除超级星
                if(isSuperStar(itor->staketo)){
                    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
                    auto sup_itor = sup_idx.find(itor->staketo);
                    sup_idx.erase(sup_itor);
                }
                else { graphene_assert(false, "StarPlan Contract Error: already stake ! ");}
            }else{
                graphene_assert(false, "StarPlan Contract Error: can't support other reason ! ");
            }
            // 2 解除抵押提现
            inline_transfer(_self , acc_id , coreAsset , itor->amount, unstake_withdraw.c_str(),unstake_withdraw.length());
            itor++;
            sta_idx.erase(itor_bak);
        }else{
            itor++;
        }
    }
}
bool starplan::isAccount(std::string accname)
{
    bool retValue = false;
    int64_t acc_id = get_account_id(accname.c_str(), accname.length());
    if(acc_id != -1){ retValue = true; }
    return retValue;
}
bool starplan::isInit()
{
    bool retValue = false;
    // 1 校验底池是否已经初始化
    auto itor = tbglobals.find(0); 
    // 2 校验当前轮资金池是否已经初始化
    auto itor2 = tbrounds.find(0);
    if(itor != tbglobals.end() && itor2 != tbrounds.end()){ retValue = true;}
    return retValue;
}
bool starplan::isInviter(std::string accname)
{
    // 验证邀请账户是否为大行星或者超级星
    bool retValue = false;
    uint64_t inviter_id = get_account_id(accname.c_str(), accname.length());
    auto big_idx = tbbigplanets.get_index<N(byaccid)>();
    auto big_itor = big_idx.find(inviter_id);
    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
    auto sup_itor = sup_idx.find(inviter_id);
    if(big_itor != big_idx.end() || sup_itor != sup_idx.end())
        retValue = true;
    return retValue;
}
bool starplan::isSuperStar(uint64_t sender)
{
    bool retValue = false;
    auto sup_idx = tbsuperstars.get_index<N(byaccid)>();
    auto sup_itor = sup_idx.find(sender);
    if(sup_itor != sup_idx.end()) { retValue = true; }
    return retValue;
}
bool starplan::addSuperStar(uint64_t sender)
{
    tbsuperstars.emplace(_self,[&](auto &obj) {                 //创建超级星
        obj.index                   = tbsuperstars.available_primary_key();
        obj.id                      = sender;
        obj.create_time             = get_head_block_time();  
        obj.create_round            = currentRound();
        obj.vote_num                = 0; 
    });
    return true;
}
bool starplan::isSmallPlanet(uint64_t sender)
{
    bool retValue = false;
    auto small_idx = tbsmallplans.get_index<N(byaccid)>();
    auto small_itor = small_idx.find(sender);
    if(small_itor != small_idx.end()) { retValue = true; }
    return retValue;
}
bool starplan::addSmallPlanet(uint64_t sender)
{
    if(!isSmallPlanet(sender)){                                                  //创建小行星
        tbsmallplans.emplace(_self,[&](auto &obj){
            obj.index                   = tbsmallplans.available_primary_key();
            obj.id                      = sender;
            obj.create_time             = get_head_block_time();
            obj.create_round            = currentRound();
        });
        return true;
    }
    return false;
}
bool starplan::isBigPlanet(uint64_t sender)
{
    bool retValue = false;
    auto big_idx = tbbigplanets.get_index<N(byaccid)>();
    auto big_itor = big_idx.find(sender);
    if(big_itor != big_idx.end()) { retValue = true; }
    return retValue;
}
bool starplan::addBigPlanet(uint64_t sender)
{
    tbbigplanets.emplace(_self,[&](auto &obj){                            //创建一个大行星
            obj.index                   = tbbigplanets.available_primary_key();
            obj.id                      = sender;
            obj.create_time             = get_head_block_time();
            obj.create_round            = currentRound();
        });
    return true;
}
bool starplan::hasInvited(uint64_t original_sender,std::string inviter)
{
    bool retValue = false;
    auto invite_idx = tbinvites.get_index<N(byaccid)>();
    auto invite_itor = invite_idx.find(original_sender);
    if(invite_itor != invite_idx.end()) { retValue = true; }
    return retValue;
}

bool starplan::bSmallRound()
{
    bool retValue = false;
    // 获取最后一个大行星
    auto big_itor = tbbigplanets.end();
    if(big_itor == tbbigplanets.begin()){
        return retValue;
    }
    big_itor--;
    graphene_assert(get_head_block_time() > (big_itor->create_time), "StarPlan Contract Error: big planet create time is error ");
    bool isDelay = (get_head_block_time() - (big_itor->create_time)) > delaytime;
    auto round_itor = tbrounds.end();
    graphene_assert(round_itor != tbrounds.begin(), "StarPlan Contract Error: Found round table wrong! ");
    round_itor--;
    bool isFull = round_itor->current_round_invites >= roundSize;
    if( isDelay || isFull) { retValue = true; }
    return retValue;
}
uint64_t starplan::currentRound()
{
    auto round_itor = tbrounds.end();
    graphene_assert(round_itor != tbrounds.begin(), "StarPlan Contract Error: Found round table wrong! ");
    round_itor--;
    return round_itor->round;
}
void starplan::invite(uint64_t original_sender,std::string inviter)
{
    uint64_t inviter_id = get_account_id(inviter.c_str(), inviter.length());
    if(inviter_id == -1)
        inviter_id = defaultinviter;
    if(!hasInvited(original_sender,inviter)){                                 //不存在邀请关系则创建，
        tbinvites.emplace(_self,[&](auto &obj) {
            obj.index                   = tbinvites.available_primary_key();
            obj.invitee                 = original_sender;
            obj.inviter                 = inviter_id;
            obj.enabled                 = false;       
            obj.create_round            = currentRound();                      //升级为大行星或者超级星时，重新设置
            obj.create_time             = get_head_block_time();
        });
    }
}
void starplan::actinvite(uint64_t original_sender)
{
    auto invite_idx = tbinvites.get_index<N(byaccid)>();
    auto invite_itor = invite_idx.find(original_sender);
    invite_idx.modify(invite_itor,_self,[&](auto &obj){                        
        obj.enabled                 = true;
        obj.create_round            = currentRound();
        obj.create_time             = get_head_block_time();
    });
    // 当前轮邀请数自增1
    auto round_itor = tbrounds.end();
    graphene_assert(round_itor != tbrounds.begin(), "StarPlan Contract Error: Found round table wrong! ");
    round_itor--;
    tbrounds.modify(*round_itor,_self,[&](auto &obj){                        
        obj.current_round_invites   = obj.current_round_invites + 1;
    });
}

void starplan::vote(uint64_t original_sender,std::string superstar)
{
    uint64_t super_id = get_account_id(superstar.c_str(), superstar.length()); 
    tbvotes.emplace(_self,[&](auto &obj) {
        obj.index                   = tbvotes.available_primary_key();
        obj.round                   = currentRound();
        obj.stake_amount            = y * precision;
        obj.from                    = original_sender; 
        obj.to                      = super_id;           
        obj.vote_time               = get_head_block_time();
    });
}
void starplan::addStake(uint64_t sender,uint64_t amount,uint64_t to,std::string reason)
{
    tbstakes.emplace(_self,[&](auto &obj) {
        obj.index                   = tbstakes.available_primary_key();
        obj.account                 = sender;
        obj.amount                  = amount;
        obj.end_time                = get_head_block_time() + delayDay; 
        obj.staketo                 = to;
        obj.reason                  = reason;
    });
}

void starplan::sendInviteReward(uint64_t sender)
{
    std::string   inviter_withdraw     = "inviter withdraw 1 GXC"; //提现一个1GXC到邀请人账户
    auto round_itor = tbrounds.end();
    graphene_assert(round_itor != tbrounds.begin(), "StarPlan Contract Error: Found round table wrong! ");
    round_itor--;
    auto invite_idx = tbinvites.get_index<N(byaccid)>();
    auto invite_itor = invite_idx.find(sender);
    tbrounds.modify(*round_itor, _self, [&](auto &obj){                               //修改奖池金额
            obj.random_pool_amount      = obj.random_pool_amount + z3 * precision;
            if(invite_itor->inviter == 0)
                obj.invite_pool_amount  = obj.invite_pool_amount + (z1 + z2) * precision;
            else
                obj.invite_pool_amount  = obj.invite_pool_amount + z1 * precision;
    });
    if(invite_itor->inviter != 0)
        inline_transfer(_self , invite_itor->inviter , coreAsset , z2 * precision,inviter_withdraw.c_str(),inviter_withdraw.length());
    
}
void starplan::updateActivePlanetsbybig(uint64_t sender)
{
    auto invite_idx = tbinvites.get_index<N(byaccid)>();
    auto invite_itor = invite_idx.find(sender);
    auto act_idx = tbactiveplans.get_index<N(byaccid)>();
    auto act_itor = act_idx.find(invite_itor->inviter);
    if(act_itor != act_idx.end()){
        act_idx.modify(act_itor,_self,[&](auto &obj){                                   //修改活力星
            if(obj.invite_count == 4){
                obj.invite_count = 0;
                obj.create_round = currentRound();
                obj.weight       = weight;
            }else{
                obj.invite_count = obj.invite_count + 1;
            }
        });
    }else{
        tbactiveplans.emplace(_self,[&](auto &obj){                                      //创建活力星
            obj.index           = tbactiveplans.available_primary_key();
            obj.id              = invite_itor->inviter;
            obj.invite_count    = 0;
            obj.create_time     = get_head_block_time();
            obj.create_round    = 0;
            obj.weight          = 0;
        });
    }
}
void starplan::updateActivePlanetsbysuper(uint64_t sender)
{
    auto act_idx = tbactiveplans.get_index<N(byaccid)>();
    auto act_itor = act_idx.find(sender);
    if(act_itor != act_idx.end()){
        act_idx.modify(act_itor,_self,[&](auto &obj){                                   //修改活力星
            obj.invite_count    = 0;
            obj.create_round    = currentRound();
            obj.weight          = weight;
        });
    }else{
        tbactiveplans.emplace(_self,[&](auto &obj){                                      //创建活力星
            obj.index           = tbactiveplans.available_primary_key();
            obj.id              = sender;
            obj.invite_count    = 0;
            obj.create_time     = get_head_block_time();
            obj.create_round    = currentRound();
            obj.weight          = weight;
        });
    }
}

void starplan::calcCurrentRoundPoolAmount()
{
    // 1、获取平均奖励池
    auto round_itor = tbrounds.end();
    graphene_assert(round_itor != tbrounds.begin(), "StarPlan Contract Error: Found round table wrong! ");
    round_itor--;

    // 2、默认底池
    auto pool_amount = roundAmount * precision + round_itor->invite_pool_amount;
    // 3、超过四小时，每小时减少底池金额
    auto x = currentRound()%bigRoundSize + 1;
    // 4、计算当前小轮的运行时间
    if(get_head_block_time() - round_itor->start_time > decayTime){
        auto dursize = ((get_head_block_time() - round_itor->start_time) / decayDur) + 1;
        dursize = dursize > maxDecayCount ? maxDecayCount:dursize;
        graphene_assert(pool_amount > (dursize * x), "PoolAmount is error !");
        pool_amount = pool_amount - dursize * x;
    }
    // 5、修改当前轮底池 pool_amount
    tbrounds.modify(*round_itor, _self, [&](auto &obj){                               //修改奖池金额pool_amount
        obj.pool_amount      = pool_amount;
    });
    // 6、修改总的资金池
    auto sub_amount = pool_amount - round_itor->invite_pool_amount;
    auto g_itor = tbglobals.find(0);
    tbglobals.modify(g_itor,_self,[&](auto &obj){                               
        obj.pool_amount      = obj.pool_amount - pool_amount;
    });
}
void starplan::updateActivePlanets()
{
    // 更新活力星的权重
    auto act_itor = tbactiveplans.begin();
    for( ; act_itor != tbactiveplans.end(); act_itor++){
        tbactiveplans.modify(act_itor,_self,[&](auto &obj){                           //修改活力星的权重
            uint64_t bDecay_prec = bDecay * 1000;
            uint64_t new_weight  = obj.weight * bDecay_prec / 1000;
            obj.weight      = new_weight;
        });
    }
}

void starplan::randomReward()
{
    std::string   random_withdraw      = "random withdraw";        //随机提现资产
    // 1 获取本轮所有大行星
    std::vector<uint64_t> cround_big_list;
    auto big_idx = tbbigplanets.get_index<N(byround)>();
    auto round = currentRound();
    auto itor = big_idx.find(round);
    while(itor != big_idx.end() && itor->create_round == round){
        cround_big_list.push_back(itor->id);
        itor++;
    }
    // 2 从列表中随机选取10个大行星
    auto bigplanet_size = cround_big_list.size() > 10 ? 10 : cround_big_list.size();
    std::vector<uint64_t> random_list;
    int64_t block_num = get_head_block_num();
    uint64_t block_time = get_head_block_time();
    std::string random_str = std::to_string(block_num) + std::to_string(block_time);
    checksum160 sum160;
    ripemd160(const_cast<char *>(random_str.c_str()), random_str.length(), &sum160);
    for(uint64_t i = 0; i<bigplanet_size; i++){
        auto j = i;
        while(true){
            uint8_t share = (uint8_t)(sum160.hash[j % 20] * (j + 1));
            uint8_t number = share % bigplanet_size;
            auto it = std::find(random_list.begin(),random_list.end(),number);
            if(it != random_list.end()){
                j++;
                continue;
            }else{
                random_list.push_back(number);
                break;
            }
        }
    }
    auto round_itor = tbrounds.end();
    graphene_assert(round_itor != tbrounds.begin(), "StarPlan Contract Error: Found round table wrong! ");
    round_itor--;
    auto total_amount = round_itor->random_pool_amount;
    auto price = total_amount / bigplanet_size;
    
    // 3 给10个大行星平均分发奖励
    for(auto i =0 ; i< bigplanet_size;i++){
        auto index = random_list[i];
        auto to = cround_big_list[index];
        if(i == bigplanet_size -1){
            inline_transfer(_self , to , coreAsset , total_amount, random_withdraw.c_str(),random_withdraw.length());
        }else{
            inline_transfer(_self , to , coreAsset , price, random_withdraw.c_str(),random_withdraw.length());
            total_amount -= price;
        }
    }
    // 4 修改当前轮底池
    tbrounds.modify(*round_itor, _self, [&](auto &obj){                               //修改奖池金额pool_amount
        obj.random_pool_amount      = 0;
    });
}
void starplan::rewardBigPlanet()
{
    std::string   bigplanet_withdraw   = "bigplanet withdraw";     //大行星奖励刮分
    // 1 获取本轮所有大行星
    std::vector<uint64_t> cround_big_list;
    auto big_idx = tbbigplanets.get_index<N(byround)>();
    auto round = currentRound();
    auto itor = big_idx.find(round);
    while(itor != big_idx.end() && itor->create_round == round){
        cround_big_list.push_back(itor->id);
        itor++;
    }
    // 2 平均分配百分之10的奖励
    auto round_itor = tbrounds.end();
    auto bigplanet_size = cround_big_list.size();
    graphene_assert(round_itor != tbrounds.begin(), "StarPlan Contract Error: Found round table wrong! ");
    round_itor--;
    uint64_t upayBackPercent = payBackPercent * 100;
    uint64_t total_amount = (round_itor->pool_amount) * upayBackPercent / 100 ;
    uint64_t price = total_amount / bigplanet_size;
    // 2.1 修改当前轮底池
    tbrounds.modify(*round_itor, _self, [&](auto &obj){                               //修改奖池金额pool_amount
        obj.pool_amount         = obj.pool_amount - total_amount;
    });
    for(auto i = 0; i< bigplanet_size ; i++){
        auto to = cround_big_list[i];
        if(i == bigplanet_size-1)
            inline_transfer(_self , to , coreAsset , total_amount, bigplanet_withdraw.c_str(),bigplanet_withdraw.length());
        else{
            inline_transfer(_self , to , coreAsset , price, bigplanet_withdraw.c_str(),bigplanet_withdraw.length());
            total_amount -= price;
        }
    }
}
void starplan::rewardActivePlanet()
{
    std::string   actplanet_withdraw   = "active planet withdraw"; //活力星奖励刮分
    // 1 获取所有活力星
    auto act_idx = tbactiveplans.get_index<N(byweight)>();
    uint64_t total_weight = 0;
    auto itor = act_idx.upper_bound(0);                     //weight = 0, 权重大于0的活力星
    auto itor_bak = itor;                                   //备份迭代器
    while(itor != act_idx.end() && itor->weight > 0){
        total_weight += itor->weight;
        itor++;
    }
    // 2 提现资产
    auto round_itor = tbrounds.end();
    graphene_assert(round_itor != tbrounds.begin(), "StarPlan Contract Error: Found round table wrong! ");
    round_itor--;
    uint64_t uactivePercent = activePercent * 100;
    uint64_t total_amount = (round_itor->pool_amount) * uactivePercent / 100 ;
    // 2.1 修改当前轮底池
    tbrounds.modify(*round_itor, _self, [&](auto &obj){                               //修改奖池金额pool_amount
        obj.pool_amount  = obj.pool_amount - total_amount;
    });
    while(itor_bak != act_idx.end() && itor_bak->weight > 0){
        auto to = itor_bak->id;
        uint64_t amount = total_amount * itor_bak->weight / total_weight;
        inline_transfer(_self , to , coreAsset , amount, actplanet_withdraw.c_str(),actplanet_withdraw.length());
        total_amount -= amount;
        itor_bak++;
        auto itor_check =itor_bak;
        itor_check++;
        if(itor_check == act_idx.end())
            break;
    }
    // 3 最后一个活力星提现
    auto ritor = act_idx.end();
    ritor--;
    auto to = ritor->id;
    inline_transfer(_self , to , coreAsset , total_amount, actplanet_withdraw.c_str(),actplanet_withdraw.length());
}
void starplan::rewardSuperStar()
{
    const std::string   supstar_withdraw     = "superstar withdraw";     //超级星奖励刮分
    // 1 获取所有超级星
    uint64_t total_vote = 0;
    for(auto itor = tbsuperstars.begin(); itor != tbsuperstars.end();itor++ ){
        total_vote += itor->vote_num;
    }
    // 2 提现资产
    auto round_itor = tbrounds.end();
    graphene_assert(round_itor != tbrounds.begin(), "StarPlan Contract Error: Found round table wrong! ");
    round_itor--;
    uint64_t total_amount = round_itor->pool_amount;
    // 2.1 修改当前轮底池
    tbrounds.modify(*round_itor, _self, [&](auto &obj){                               //修改奖池金额pool_amount
        obj.pool_amount         = 0;
        obj.invite_pool_amount  = 0;
    });
    for(auto itor = tbsuperstars.begin(); ;itor++ ){
        auto to = itor->id;
        uint64_t amount = total_amount * itor->vote_num / total_vote;
        inline_transfer(_self , to , coreAsset , amount, supstar_withdraw.c_str(),supstar_withdraw.length());
        total_amount -= amount;
        auto itor_bak = itor;
        itor_bak++;
        itor_bak++;
        if(itor_bak ==tbsuperstars.end() )
            break;
    }
    // 3 最后一个超级星提现
    auto ritor = tbsuperstars.end();
    ritor--;
    auto to = ritor->id;
    inline_transfer(_self , to , coreAsset , total_amount, supstar_withdraw.c_str(),supstar_withdraw.length());
}
void starplan::createnewround()
{
    // 1 结束当前轮，修改round表和global表
    auto round_itor = tbrounds.end();
    graphene_assert(round_itor != tbrounds.begin(), "StarPlan Contract Error: Found round table wrong! ");
    round_itor--;
    tbrounds.modify(*round_itor, _self, [&](auto &obj){                               
        obj.end_time            = get_head_block_time();
    });
    // 2 创建新的一轮
    auto g_itor = tbglobals.find(0);
    tbglobals.modify(g_itor,_self,[&](auto &obj){                               
        obj.current_round      = obj.current_round + 1;
    });
    tbrounds.emplace(_self,[&](auto &obj) {
        obj.round                   = tbrounds.available_primary_key();
        obj.current_round_invites   = 0;       
        obj.pool_amount             = 0;
        obj.random_pool_amount      = 0;
        obj.invite_pool_amount      = 0;
        obj.start_time              = get_head_block_time();
        obj.end_time                = 0;
    });
}
bool starplan::canupdatesmall(uint64_t sender)
{
    bool retValue = false;
    auto vot_idx = tbvotes.get_index<N(byfrom)>();
    uint64_t total_vote = 0;
    auto itor = vot_idx.find(sender); 
    for(;itor != vot_idx.end();itor++){
        if(itor->from == sender){
            total_vote += itor->stake_amount;
        }else{
            break;
        }
    }
    if(total_vote>=y*precision){ retValue = true; }
    return retValue;
}
void starplan::deletevote(uint64_t sender,uint64_t time)
{
    auto vot_idx = tbvotes.get_index<N(byfrom)>();
    auto itor = vot_idx.find(sender);
    for(;itor != vot_idx.end();){
        if(itor->from == sender){
            if(itor->vote_time == time + delayDay){
                itor = vot_idx.erase(itor);
                break;
            }else{ itor++ ;}
        }else{
            break;
        }
    }
}