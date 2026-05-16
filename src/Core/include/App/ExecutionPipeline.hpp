#ifndef EXECUTION_PIPELINE_HPP
#define EXECUTION_PIPELINE_HPP

namespace stnks
{
    class ExecutionPipeline
    {
    public:
        virtual void Init() = 0;
        virtual void UpdateUI() = 0;
        virtual void Update() = 0;
    };
} // namespace stnks

#endif