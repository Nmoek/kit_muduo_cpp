/**
 * @file config.h
 * @brief 配置系统
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-30 01:40:11
 * @copyright Copyright (c) 2026 Kewin Li
 */

#ifndef __KIT_CONFIG_H__
#define __KIT_CONFIG_H__

#include "base/config_codec.h"
#include "base/config_codec_policy.h"
#include "base/config_context.h"

#include <atomic>
#include <cctype>
#include <memory>
#include <mutex>
#include <algorithm>
#include <string>
#include <functional>
#include <yaml-cpp/yaml.h>
#include <vector>
#include <map>
#include <stdint.h>


namespace kit_muduo {

template<typename Policy, typename Scope>
class ConfigUpdateBatch;

template<typename T, typename Policy, typename Codec>
class ConfigVar;

/**
 * @brief 延迟提交闭包对象
 */
class ConfigPreparedUpdate
{
public:
    ConfigPreparedUpdate(ConfigPreparedUpdate&& other) noexcept;
    ConfigPreparedUpdate& operator=(ConfigPreparedUpdate&& other) noexcept;
    
    ConfigPreparedUpdate(const ConfigPreparedUpdate&) = delete;
    ConfigPreparedUpdate& operator=(const ConfigPreparedUpdate&) = delete;

    ~ConfigPreparedUpdate() = default;

    /**
     * @brief 特别注意 这是一次完整配置提交不能抛异常
     */
    void commit() noexcept;

private:
    template<class T, class Policy, class Codec>
    friend class ConfigVar;

    explicit ConfigPreparedUpdate(std::function<void()> commit);

private:
    std::function<void()> commit_cb_;
    bool committed_{false};
};




template<typename Policy> 
class ConfigVarBase
{
public:
    using Ptr =  typename std::shared_ptr<ConfigVarBase<Policy>>;
    using Node = typename Policy::Node;

    /**
     * @brief 配置项虚基类构造函数
     * @param[in] name  配置项名称 
     * @param[in] description 配置项描述
     */
    ConfigVarBase(const std::string &name, const std::string &description = "")
        :name_(name) 
        ,description_(description)
    {
        std::for_each(name_.begin(), name_.end(), [](auto &c){ c = std::tolower(c); });
    }

    /**
     * @brief 配置项虚基类析构函数
     */
    virtual ~ConfigVarBase() = default;

    /**
     * @brief 获取配置项名称
     * @return const std::string& 
     */
    const std::string & name() const noexcept { return name_; }

    /**
     * @brief 获取配置项描述
     * @return const std::string& 
     */
    const std::string& description() const noexcept {return description_;}

    /**
     * @brief 将配置项真值转换为配置节点
     * @return Node 
     */
    virtual Node toNode() = 0;

    /**
     * @brief 将配置节点转换为配置项真值-待提交对象
     * @param node 
     * @return true 
     * @return false 
     */
    virtual ConfigPreparedUpdate prepareNode(const Node &node, const ConfigContext &context) = 0;

    /**
     * @brief 将配置项真值转换为string类型
     * @return std::string 
     */
    virtual std::string toString() = 0;


    /**
     * @brief 获取配置项类型名称
     * @return std::string 
     */
    virtual std::string typeName() const = 0;

protected:
    /// @brief 配置项名称
    std::string name_;
    /// @brief 配置项描述
    std::string description_;
};


/**
 * @brief 配置项
 * @tparam T 配置项真值类型
 * @tparam Policy 序列化/反序列化策略
 */
template<typename T, typename Policy, typename Codec = ConfigCodec<T, Policy>>
class ConfigVar final: public ConfigVarBase<Policy>
    ,public std::enable_shared_from_this<ConfigVar<T, Policy, Codec>>
{
public:
    using Ptr = typename std::shared_ptr<ConfigVar<T, Policy>>;

    using ChangeCallback = std::function<void (const T& old_value, const T& new_value)>;

    using Node = typename Policy::Node;


    /**
     * @brief 配置项类构造函数
     * @param[in] name 配置项名称，作为key值
     * @param[in] default_value 配置项的默认值
     * @param[in] description 配置项的描述
     */
    ConfigVar(const std::string &name,  T default_value = T{}, const std::string description = "")
        :ConfigVarBase<Policy>(name, description)
        ,val_(std::make_shared<const T>(std::move(default_value)))
    {

    }

        /**
     * @brief 获取配置项，传入什么类型就返回什么类型
     * @return const T 
     */
    std::shared_ptr<const T> value() const
    {
        return std::atomic_load_explicit(&val_, std::memory_order_acquire);
    }

    Node toNode() override
    {
        return Codec::Encode(*value());
    }

    ConfigPreparedUpdate prepareNode(const Node &node, const ConfigContext &context) override
    {
        try {
            return prepareValue(Codec::Decode(node));
        } catch (const ConfigError &e) {
 
            ConfigError new_e = context.node_path.empty()
                ? e
                : e.prependPath(context.node_path);
            throw new_e.withFallbackContext(context);
        }
    }

    std::string toString() override 
    {
        return Policy::Serialize(toNode());;
    }


    /**
     * @brief 获取配置项类型名称
     * @return std::string 
     */
    std::string typeName() const override { return typeid(T).name();}


    /**
     * @brief 为配置项绑定一个回调函数
     * @param[in] cb 指定的回调函数
     * @return uint64_t 返回一个唯一标识回调函数的key值
     */
    uint64_t addListener(ChangeCallback cb) 
    {
        //内部自己生成一个key(id)来唯一标识回调函数 key不能重复
        static uint64_t cb_id = 0;
        
        std::lock_guard<std::mutex> lock(mtx_);
        change_cbs_[++cb_id] = cb;
        return cb_id;
    }

    /**
     * @brief 删除配置项上的某一回调函数
     * @param[in] key 唯一标识回调函数的key值
     */
    void delListener(uint64_t id) 
    {
        std::lock_guard<std::mutex> lock(mtx_);
        change_cbs_.erase(id);
    }
  
    /**
     * @brief 获取配置项上的某一回调函数
     * @param[in] key 唯一标识回调函数的key值
     * @return on_change_cb 
     */
    ChangeCallback getListener(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = change_cbs_.find(id);
        
        return it == change_cbs_.end() ? nullptr : it->second;
    }

    /**
     * @brief 清除配置项上的所有回调函数
     */
    void clearListener()
    {
        std::lock_guard<std::mutex> lock(mtx_);
        change_cbs_.clear();
    }
private:
    template<typename, typename>
    friend class ConfigUpdateBatch;

    /**
     * @brief 为环境变量准备 真值更新接口
     * @param value 
     * @param context 
     * @return PreparedUpdate 
     */
    ConfigPreparedUpdate prepareValue(T value) const
    {
        
        // 闭包提交
        return ConfigPreparedUpdate([
            new_val = std::move(std::make_shared<const T>(std::move(value)))
            ,this_weak_ptr = this->weak_from_this()]()
        {
            auto this_ptr = this_weak_ptr.lock();
            if(!this_ptr)
            {
                return;
            }
            this_ptr->commitValue(new_val);
        });
    }

    void commitValue(std::shared_ptr<const T> new_val) const noexcept
    {
        std::atomic_store_explicit(&val_, std::move(new_val), std::memory_order_release);
    }

private:
    /// @brief 配置项数据
    mutable std::shared_ptr<const T> val_;
    /// @brief 配置项所绑定的回调函数容器
    std::map<uint64_t, ChangeCallback> change_cbs_;
    mutable std::mutex mtx_;
};



/**********  Config helper函数**********/

ConfigContext RegistryContext(const std::string& node_path);


/**
* @brief 检查单个配置路径段是否合法
* @param segment 单个 YAML/JSON map key，不能包含点分隔符
* @return true
* @return false
*/
bool CheckConfigPathSegment(const std::string &segment);

/**
 * @brief 单个配置路径段转 ASCII 小写归一化处理
 * @param segment
 * @return std::string 
 */
std::string NormalizeConfigPathSegment(const std::string& segment);

/**
 * @brief 全路径校验
    避免 .http  http.. http. 等情况出现
 * @param node_path 
 * @return std::string 
 */
std::string NormalizeFullConfigPath(const std::string& node_path);


/**
 * @brief 拆分配置项路径名称
 * @param input 
 * @return std::vector<std::string> 
 */
std::vector<std::string> SplitConfigNodePath(const std::string& input);

/**
 * @brief 拼接配置项路径名称
 * @param node_path 
 * @return std::string 
 */
std::string SpliceConfigNodePath(const std::string& node_path);

/**
 * @brief 点分连接路径
 * @param prefix 
 * @param node_path 
 * @return std::string 
 */
std::string JoinConfigPath(const std::string& prefix, const std::string& node_path);

/**
 * @brief 判断是否存在 重叠层次的路径
 * @param ancestor 
 * @param descendant 
 * @return true 
 * @return false 
 */
bool IsAncestorConfigPath(const std::string& ancestor, const std::string& descendant);

std::string ReadConfigFile(const std::string &file_path);



/**********  Config helper函数**********/

/**
 * @brief 配置中合法但未知key的处理策略
 */
enum class ConfigLoadPolicy
{
    kIgnore, // 忽略
    kReject, // 明确拒绝
};

// 模版占位符 用于生成不同实例
struct ProductionScope {}; // 默认使用生产占位

template<typename Policy, typename Scope = ProductionScope>
class Config
{
public:
    using VarBase = ConfigVarBase<Policy>;
    using ConfigVarMap = std::map<std::string, typename VarBase::Ptr>;
    template<typename T>
    using Var = ConfigVar<T, Policy>;

    using Node = typename Policy::Node;

    /**
     * @brief 检查是否有层次重叠的节点(非常重要)
     * @param path 
     * @return true 
     * @return false 
     */
    bool hasPathConflictUnLocked(const std::string& path) const
    {
        for(const auto& [registered, var] : vars_)
        {
            if(registered == path
                || IsAncestorConfigPath(registered, path)
                || IsAncestorConfigPath(path, registered))
            {
                return registered != path;
            }
        }
        return false;
    }

    /**
     * @brief 查询配置项，如果没有就会创建
     * @tparam T 配置项的参数类型
     * @param[in] name 配置项名称
     * @param[in] default_value 配置项默认值
     * @param[in] description 配置项描述
     * @return ConfigVar<T>::Ptr 
     */
    template<class T>
    typename ConfigVar<T, Policy>::Ptr lookAndCreate(const std::string &name, 
        const T& default_value =  T{},
        const std::string &description = "")
    {
        const std::string& node_path = NormalizeFullConfigPath(name);
        std::lock_guard<std::mutex> lock(mtx_);
        auto &vars = vars_;

        ensureFrozenUnLocked();

        //判断查询的配置项是否存在
        auto it = vars.find(node_path);
        if(it != vars.end()) //配置项存在
        {
            auto var = std::dynamic_pointer_cast<ConfigVar<T, Policy>>(it->second);
            if(!var)
            {
                throw ConfigError(
                    RegistryContext(node_path),
                    "configuration key already exists with another type");
            }
            return var;
        }
        // 检查子节点和祖先节点
        /* 避免这样一种情况:
            http --> 已经作为标量
            http.port --> 又出现了层次结构 这是冲突的
        */
        if(hasPathConflictUnLocked(node_path))
        {
            throw ConfigError(RegistryContext(node_path),
                "config key conflicts with an ancestor or descendant: " + node_path);
        }

        auto var = std::make_shared<ConfigVar<T, Policy>>(node_path, default_value, description);

        vars.emplace(node_path, var);
      
        return var;

    }

    /**
     * @brief 查询配置项，如果没有返回nullptr
     * @tparam T 配置项的参数类型
     * @param[in] name  配置项名称
     * @return ConfigVar<T>::Ptr 
     */
    template<class T>
    typename ConfigVar<T, Policy>::Ptr lookUp(const std::string &name)
    {
        const std::string &node_path = NormalizeFullConfigPath(name);

        std::lock_guard<std::mutex> lock(mtx_);

        auto it = vars_.find(node_path);
        if(it == vars_.end())
        {
            return nullptr;
        }

        auto var = std::dynamic_pointer_cast<
            ConfigVar<T, Policy>>(it->second);
        if(!var)
        {
            throw ConfigError(
                RegistryContext(node_path),
                "configuration key type mismatch; registered value type is "
                    + it->second->typeName()
                    + "; requested ConfigVar type is "
                    + typeid(ConfigVar<T, Policy>).name());
        }
        return var;
    }

    /**
     * @brief 返回配置项的基类指针
     * @param[in] name 配置项名称 
     * @return ConfigVarBase::Ptr 
     */
    typename VarBase::Ptr lookUpBase(const std::string& name)
    {
        const std::string &node_path = NormalizeFullConfigPath(name);

        std::lock_guard<std::mutex> lock(mtx_);

        auto it = vars_.find(node_path);
        return it == vars_.end() ?nullptr : it->second;
    }


    /**
     * @brief 通过配置节点，进行配置提交
     * @param root 配置节点根节点
     * @param context 配置上下文
     * @param policy 提交策略
     */
    void load(const Node& root, ConfigContext context, ConfigLoadPolicy policy = ConfigLoadPolicy::kReject)
    {
        // 注意原则: 约定大于配置, 从程序约定出发
        ConfigVarMap snapshot_vars;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            ensureFrozenUnLocked();
            snapshot_vars = vars_;
        }

        if(!Policy::IsMap(root))
        {
            throw ConfigError(Policy::Context(root).withSourceIfEmpty(context.source),
                "configuration document root must be a map");
        }

        std::set<std::string> normalized_roots;
        ConfigUpdateBatch<Policy, Scope> batch(*this);

        Policy::VisitMap(root, [&](const std::string& key, const Node& child){

            const std::string &source = context.source;

            const std::string child_path = NormalizeChildNode(key, child, "", source);

            if(!normalized_roots.insert(child_path).second)
            {
                ConfigContext context = Policy::Context(child)
                    .withSourceIfEmpty(source);
                context.node_path = child_path;

                throw ConfigError(std::move(context),
                    "duplicate root key after normalization");
            }

            CollectPrepareUpdates(snapshot_vars, child_path, child, policy, source, batch);
        });

        // 所有配置项都无误后统一提交
        batch.commit();
    }


    /**
     * @brief 通过指定配置文件加载，进行配置提交
     * @param file_path 配置文件路径
     * @param policy 提交策略
     * @return true 
     * @return false 
     */
    void load(const std::string& file_path, ConfigLoadPolicy policy = ConfigLoadPolicy::kReject)
    {

        const auto &content = ReadConfigFile(file_path);

        try {
            return load(Policy::Parse(content), ConfigContext{
                .source = file_path,
            }, policy);
        } catch (const ConfigError &e) {
            throw e.withSourceIfEmpty(file_path);
        }
    }

    /**
     * @brief 通过回调函数进行配置提交
     * @tparam Callback 
     * @param prepare_cb 
     */
    template<typename Callback>
    void load(Callback &&prepare_cb)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            ensureFrozenUnLocked();
        }

        ConfigUpdateBatch<Policy, Scope> batch(*this);
        std::forward<Callback>(prepare_cb)(batch);
        batch.commit();
    }

    /**
     * @brief 轮询对所有配置项进行某个操作
     * @param[in] cb 指定一个函数去操作配置项
     */
    void visit(std::function<void(typename VarBase::Ptr)> callback)
    {
        if(!callback)
        {
            throw std::invalid_argument("configuration visitor must not be empty");
        }

        ConfigVarMap snapshot;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            snapshot = vars_;
        }

        for(const auto& item : snapshot)
        {
            callback(item.second);
        }
    }

    /**
     * @brief 将点分路径集合重建为配置节点树
     * @return Node 
     */
    Node buildNodeTree()
    {
        struct NodeTree
        {
            /// @brief 每个节点对应的基础数据类型, 如果存在值说明是一个叶子节点
            typename VarBase::Ptr base_var{nullptr};
            /// @brief 每个节点下可能存在的树结构
            std::map<std::string, NodeTree> children;
        };

        ConfigVarMap snapshot;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            snapshot = vars_;
        }

        NodeTree tree;
        for(auto &it : snapshot)
        {
            
            NodeTree *t = &tree;
            for(auto &node_path : SplitConfigNodePath(it.first))
            {
                t = &t->children[node_path];
            }
            t->base_var = it.second;
        }

        // 递归建树
        std::function<Node(const NodeTree&)> build = [&build](const NodeTree &child) -> Node 
        {
            // base_var有值代表是叶子节点
            if(child.base_var)
            {
                //叶子节点还有子节点 这是矛盾的
                if(!child.children.empty())
                {
                    throw std::logic_error("invalid config node tree: scalar key has children: " + child.base_var->name());
                }
                return child.base_var->toNode();
            }

            // 这里允许空节点，理论上不可能出现
            Node map = Policy::MakeMap();
            for(auto &c : child.children)
            {
                Policy::Put(map, c.first, build(c.second));
            }
            return map;
        };

        return build(tree);
    }



    void freeze() noexcept
    {
        std::lock_guard<std::mutex> lock(mtx_);
        frozen_ = true;
    }

    bool isFrozen() noexcept
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return frozen_;
    }

public:
    static Config<Policy, Scope>& Instance()
    {
        static Config<Policy, Scope> config;
        return config;
    }

    static std::string ToString(const Node &node) 
    {
        return Policy::Serialize(node);
    }

private:
    template<typename, typename> 
    friend class ConfigUpdateBatch;

    Config() = default;

    /**
     * @brief 被冻结不允许写入配置
     */
    void ensureFrozenUnLocked()
    {
        if(frozen_)
        {
            throw ConfigError(RegistryContext(""),
                "configuration registry is frozen");
        }
    }

    /// @brief 确定是同一个物理内存的配置对象
    bool isOwnersUnLocked(const typename VarBase::Ptr& base_var)
    {
        if(!base_var)
        {
            return false;
        }
        auto it = vars_.find(base_var->name());
        return it != vars_.end() && it->second.get() == base_var.get();
    }

private:
    /**
    * @brief 比对当前已注册配置 点分路径最左前缀是否存在
    * @param vars 
    * @param path 
    * @return true 
    * @return false 
    */
    static inline bool HasRegisteredPrefix(const ConfigVarMap& vars, const std::string& path)
    {
        const std::string prefix = path + ".";
        auto it = vars.lower_bound(prefix);
        return it != vars.end()
            && it->first.compare(0, prefix.size(), prefix) == 0;
    }

    static std::string NormalizeChildNode(const std::string& node_path,
        const Node& child,
        const std::string& parent_path,
        const std::string& source)
    {
        try {
            return NormalizeConfigPathSegment(node_path);
        } catch(const ConfigError &e) {
            ConfigContext context = Policy::Context(child)
                .withSourceIfEmpty(source);
            context.node_path = JoinConfigPath(parent_path, node_path);

            throw ConfigError(std::move(context), e.reason());
        }
    }


    /**
     * @brief 将配置项节点折叠为点分形式
     * @param prefix 
     * @param node 
     * @param out 
     * @return true 
     * @return false 
     */
    static void CollectPrepareUpdates(const ConfigVarMap &snapshot,
        const std::string &node_path, 
        const Node &node,
        ConfigLoadPolicy policy,
        const std::string& source,
        ConfigUpdateBatch<Policy, Scope>& batch)
    {
        ConfigContext cur_context = Policy::Context(node).withSourceIfEmpty(source);
        cur_context.node_path = node_path;

        auto it = snapshot.find(node_path);
        if(it != snapshot.end()) // 精确命中
        {
            batch.prepareNode(it->second, node, cur_context);
            return;
        }

        // TODO 这里数据结构应该使用 压缩路径trie树 否则退化为O(nm) n是已注册配置个数 m是每个注册项长度
        if(!HasRegisteredPrefix(snapshot, node_path)) // 判断当前前缀 是否出现在配置中
        {
            if(ConfigLoadPolicy::kReject == policy)
            {
                throw ConfigError(std::move(cur_context), "unknown configuration key");
            }
            return;
        }

        if(!Policy::IsMap(node))
        {
            throw ConfigError(std::move(cur_context),  "configuration key prefix must be a map");
        }

        // node路径去重
        std::unordered_set<std::string> normalized_child_paths;

        Policy::VisitMap(node, [&](const std::string& key, const Node& child){

            const std::string child_path = NormalizeChildNode(key, child, node_path, source);

            const std::string &new_node_path = JoinConfigPath(node_path, child_path);

            // 子节点中出现出现重复路径
            if(!normalized_child_paths.insert(child_path).second)
            {
                ConfigContext duplicate_context = Policy::Context(child)
                        .withSourceIfEmpty(source);
                duplicate_context.node_path = new_node_path;

                throw ConfigError(std::move(duplicate_context),
                    "duplicate key after ASCII lowercase normalization");
            }

            CollectPrepareUpdates(snapshot, new_node_path, child, policy, source, batch);
        });
    }

private:
    ConfigVarMap vars_;
    std::mutex mtx_;
    bool frozen_{false};
};

using YamlConfig = Config<ConfigYamlPolicy>;
using JsonConfig = Config<ConfigJsonPolicy>;


/**
 * @brief 批量延迟提交事务处理
 * @tparam Policy 
 */
template<typename Policy, typename Scope>
class ConfigUpdateBatch
{
public:
    using Node = typename Policy::Node;
    using VarBase = ConfigVarBase<Policy>;

    ConfigUpdateBatch(ConfigUpdateBatch&&) = delete;
    ConfigUpdateBatch(const ConfigUpdateBatch&) = delete;

    void prepareNode(const typename VarBase::Ptr& base_var, const Node &node, ConfigContext context)
    {
        ensureTarget(base_var, context);
        updates_.push_back(PendingUpdate{
            base_var, 
            base_var->prepareNode(node, context)
        });
    }

    template<typename T>
    void prepareValue(const std::shared_ptr<ConfigVar<T, Policy>> &var, T value, ConfigContext context)
    {
        auto base_var = std::static_pointer_cast<VarBase>(var);
        ensureTarget(base_var, context);
        updates_.push_back(PendingUpdate{
            var,
            var->prepareValue(value),
        });
    }

private:
    template<typename, typename>
    friend class Config;

    struct PendingUpdate
    {
        typename VarBase::Ptr target;
        ConfigPreparedUpdate update;
    };

    explicit ConfigUpdateBatch(Config<Policy, Scope>& owner)
        :owner_(owner)
        ,commited_(false)
    {

    }

    void ensureTarget(const typename VarBase::Ptr &base_var, const ConfigContext &context)
    {
        if(!base_var)
        {
            throw ConfigError(context, "configuration update target is null");
        }
        std::lock_guard<std::mutex> lock(owner_.mtx_);

        owner_.ensureFrozenUnLocked();
        if(!owner_.isOwnersUnLocked(base_var))
        {
            throw ConfigError(context,
                "configuration update target does not belong to registry");
        }

    }

    void commit()
    {
        std::lock_guard<std::mutex> lock(owner_.mtx_);
        owner_.ensureFrozenUnLocked();
        if(commited_)
        {
            throw ConfigError(RegistryContext(""),
                "configuration update batch already committed");
        }
        
        for(auto &p : updates_)
        {
          if(!owner_.isOwnersUnLocked(p.target))
            throw ConfigError(RegistryContext(p.target->name()),
                "configuration update target no longer belongs to Config singleton");
        }
        for(auto& p : updates_)
        {
            p.update.commit();
        }
        commited_ = true;
    }

private:
    Config<Policy, Scope>& owner_;
    std::vector<PendingUpdate> updates_;
    bool commited_{false};
};


}
#endif // __KIT_CONFIG_H__
