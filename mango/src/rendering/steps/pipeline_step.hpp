//! \file      pipeline_step.hpp
//! \author    Paul Himmler
//! \version   1.0
//! \date      2020
//! \copyright Apache License 2.0

#ifndef MANGO_PIPELINE_STEP_HPP
#define MANGO_PIPELINE_STEP_HPP

#include <graphics/command_buffer.hpp>
#include <mango/system.hpp>
#include <mango/types.hpp>
#include <graphics/uniform_buffer.hpp>

namespace mango
{
    //! \brief Base class for all optional pipeline steps in render pipelines.
    class pipeline_step : public system
    {
      public:
        //! \brief Attaches the step to the current active pipeline.
        //! \details After creation this function should be called. Does all the setup.
        virtual void attach() = 0;

        //! \brief Executes the command by inserting commands into the given \a render \a command_buffer.
        //! \param[in] command_buffer The buffer given by the \a render to execute into.
        //!\param[in] frame_uniform_buffer The \a uniform_buffer to manage uniform buffer memory.
        virtual void execute(command_buffer_ptr& command_buffer, uniform_buffer_ptr frame_uniform_buffer) = 0;

        //! \brief Custom UI function.
        //! \details This can be called by any \a ui_widget and displays settings for the active \a pipeline_step.
        //! This does not draw any window, so it needs one surrounding it.
        virtual void on_ui_widget() = 0;

      protected:
        virtual bool create()         = 0;
        virtual void update(float dt) = 0;
        virtual void destroy()        = 0;
    };
} // namespace mango

#endif // MANGO_PIPELINE_STEP_HPP
