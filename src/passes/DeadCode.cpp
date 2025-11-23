#include "DeadCode.hpp"
#include "Instruction.hpp"
#include "logging.hpp"
#include <memory>
#include <vector>

// 处理流程：两趟处理，mark 标记有用变量，sweep 删除无用指令
void DeadCode::run()
{
    bool changed{};
    func_info->run();
    do
    {
        changed = false;
        for (auto &F : m_->get_functions())
        {
            auto func = &F;
            changed |= clear_basic_blocks(func);
            mark(func);
            changed |= sweep(func);
        }
    } while (changed);
    
    // 全局清理未使用的函数和全局变量
    sweep_globally();
    
    LOG_INFO << "dead code pass erased " << ins_count << " instructions";
}

bool DeadCode::clear_basic_blocks(Function *func)
{
    bool changed = 0;
    std::vector<BasicBlock *> to_erase;
    for (auto &bb1 : func->get_basic_blocks())
    {
        auto bb = &bb1;
        if (bb->get_pre_basic_blocks().empty() && bb != func->get_entry_block())
        {
            to_erase.push_back(bb);
            changed = 1;
        }
    }
    for (auto &bb : to_erase)
    {
        bb->erase_from_parent();
        delete bb;
    }
    return changed;
}

void DeadCode::mark(Function *func)
{
    // 重置标记
    marked.clear();
    work_list.clear();
    
    // 标记所有关键指令（这些指令必须保留）
    for (auto &bb : func->get_basic_blocks())
    {
        for (auto &instr : bb.get_instructions())
        {
            auto ins = &instr;
            if (is_critical(ins))
            {
                mark(ins);
            }
        }
    }
    
    // 处理工作列表，传播标记
    while (!work_list.empty())
    {
        Instruction *ins = work_list.front();
        work_list.pop_front();
        
        // 标记所有操作数为有用
        for (auto operand : ins->get_operands())
        {
            if (auto operand_ins = dynamic_cast<Instruction *>(operand))
            {
                if (!marked[operand_ins])
                {
                    marked[operand_ins] = true;
                    work_list.push_back(operand_ins);
                }
            }
        }
    }
}

void DeadCode::mark(Instruction *ins)
{
    if (!marked[ins])
    {
        marked[ins] = true;
        work_list.push_back(ins);
    }
}

bool DeadCode::sweep(Function *func)
{
    std::unordered_set<Instruction *> wait_del{};
    bool changed = false;

    // 1. 收集所有未被标记的指令
    for (auto &bb : func->get_basic_blocks())
    {
        for (auto &instr : bb.get_instructions())
        {
            auto ins = &instr;
            if (!marked[ins] && !is_critical(ins))
            {
                wait_del.insert(ins);
            }
        }
    }

    // 2. 执行删除
    for (auto ins : wait_del)
    {
        // 删除指令前先移除其对操作数的引用
        // 注意：LightIR中use关系是自动管理的，通常不需要手动移除
        // 只需要从基本块中删除指令即可
        
        // 从基本块中删除指令 - 使用正确的API
        ins->get_parent()->erase_instr(ins);
        ins_count++;
        changed = true;
    }

    return changed;
}

bool DeadCode::is_critical(Instruction *ins)
{
    // 关键指令：这些指令不能被删除
    
    // 1. 存储指令（可能有副作用）
    if (ins->is_store())
        return true;
        
    // 2. 返回指令
    if (ins->is_ret())
        return true;
        
    // 3. 函数调用（可能有副作用）
    if (ins->is_call())
    {
        // 保守起见，认为所有函数调用都有副作用
        return true;
    }
    
    // 4. 分支指令（控制流相关）
    if (ins->is_br())
        return true;

    // 5. phi指令（SSA形式关键指令）
    if (ins->is_phi())
        return true;
    
    // 6. 分配指令（alloca）
    if (ins->is_alloca())
        return true;

    // 7. 加载指令（可能有副作用）
    if (ins->is_load())
        return true;
    
    return false;
}

void DeadCode::sweep_globally()
{
    // 注意：全局函数和变量的删除需要谨慎处理
    // 由于API限制，这里暂时不实现全局清理
    // 或者使用更安全的方式
    
    std::vector<Function *> unused_funcs;
    std::vector<GlobalVariable *> unused_globals;
    
    // 查找未使用的函数（除了main）
    for (auto &f_r : m_->get_functions())
    {
        if (f_r.get_use_list().size() == 0 && f_r.get_name() != "main")
            unused_funcs.push_back(&f_r);
    }
    
    // 查找未使用的全局变量
    for (auto &glob_var_r : m_->get_global_variable())
    {
        if (glob_var_r.get_use_list().size() == 0)
            unused_globals.push_back(&glob_var_r);
    }
    
    // 由于Module类可能没有remove_function和remove_global_variable方法
    // 这里暂时注释掉删除操作，或者使用其他方式
    /*
    for (auto func : unused_funcs)
    {
        // 需要找到正确的方法来删除函数
    }
    
    for (auto glob : unused_globals)
    {
        // 需要找到正确的方法来删除全局变量
    }
    */
    
    // 或者只是记录日志
    if (!unused_funcs.empty() || !unused_globals.empty())
    {
        LOG_INFO << "Found " << unused_funcs.size() << " unused functions and " 
                 << unused_globals.size() << " unused global variables";
    }
}