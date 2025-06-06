//	Copyright 2015 by Joseph Forgione
//	This file is part of VCC (Virtual Color Computer).
//	
//		VCC (Virtual Color Computer) is free software: you can redistribute it and/or modify
//		it under the terms of the GNU General Public License as published by
//		the Free Software Foundation, either version 3 of the License, or
//		(at your option) any later version.
//	
//		VCC (Virtual Color Computer) is distributed in the hope that it will be useful,
//		but WITHOUT ANY WARRANTY; without even the implied warranty of
//		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//		GNU General Public License for more details.
//	
//		You should have received a copy of the GNU General Public License
//		along with VCC (Virtual Color Computer).  If not, see <http://www.gnu.org/licenses/>.
//	
//		Debugger Interface - Part of the Debugger package for VCC
//		Author: Chet Simpson
#include "Debugger.h"
#include "DebuggerUtils.h"
#include "MachineDefs.h"
#include <sstream>
#include <iomanip>
#include "tcc1014mmu.h"	//  FIXME: May want this decoupled from Debugger for Memory Write
#include "audio.h"
#include "Disassembler.h"

namespace VCC { namespace Debugger
{

	void Debugger::Reset()
	{
		SectionLocker lock(Section_);

		ExecutionMode_ = ExecutionMode::Run;
		PendingCommand_ = ExecutionMode::Halt;
		HasPendingCommand_ = false;
		ProcessorState_ = CPUState();

		for (const auto& callback : RegisteredClients_)
		{
			callback.second->OnReset();
		}

		Decoder_ = std::make_unique<OpDecoder>();
		TraceEnabled_ = false;
		TraceRunning_ = false;
		TraceMarks_.clear();
	}

	void Debugger::RegisterClient(HWND window, std::unique_ptr<Client> client)
	{
		SectionLocker lock(Section_);

		if (!client)
		{
			throw std::invalid_argument("client cannot be null");
		}

		if (RegisteredClients_.find(window) != RegisteredClients_.end())
		{
			throw std::invalid_argument("Udpate client is already registered with the debugger");
		}

		RegisteredClients_[window] = move(client);
	}
	
	
	void Debugger::RemoveClient(HWND window)
	{
		SectionLocker lock(Section_);

		if (RegisteredClients_.find(window) == RegisteredClients_.end())
		{
			throw std::invalid_argument("window is not registered with the debugger");
		}

		RegisteredClients_.erase(window);
	}


	void Debugger::Halt()
	{
		ExecutionMode_ = ExecutionMode::Halt;
	}


	bool Debugger::IsHalted() const
	{
		return ExecutionMode_ == ExecutionMode::Halt;
	}


	bool Debugger::IsHalted(unsigned short& returnPc) const
	{
		if (ExecutionMode_ != ExecutionMode::Halt)
		{
			return false;
		}

		{
			SectionLocker lock(Section_);
			returnPc = ProcessorState_.PC;
		}

		return true;
	}


	bool Debugger::IsStepping() const
	{
		return ExecutionMode_ == ExecutionMode::Step;
	}



	CPUState Debugger::GetProcessorStateCopy() const
	{
		SectionLocker lock(Section_);

		return ProcessorState_;
	}


	bool Debugger::HasPendingCommandNoLock() const
	{
		return HasPendingCommand_;
	}


	Debugger::ExecutionMode Debugger::ConsumePendingCommandNoLock()
	{
		if (!HasPendingCommandNoLock())
		{
			throw std::runtime_error("No pending command to consume");
		}

		HasPendingCommand_ = false;
		return PendingCommand_;
	}


	void Debugger::SetBreakpoints(breakpointsbuffer_type breakpoints)
	{
		SectionLocker lock(Section_);

		Breakpoints_ = move(breakpoints);
		BreakpointsChanged_ = true;
	}


	bool Debugger::IsTracingEnabled() const
	{
		return TraceEnabled_;
	}

	bool Debugger::IsTracing() const
	{
		return TraceRunning_;
	}

	void Debugger::TraceCaptureInterruptRequest(unsigned char irq, long cycleTime, CPUState state)
	{
		Decoder_->CaptureInterrupt(TraceEvent::IRQRequested, Decoder_->ToIRQType(irq), cycleTime, state);
		CheckStopTrace(state);
	}

	void Debugger::TraceCaptureInterruptMasked(unsigned char irq, long cycleTime, CPUState state)
	{
		Decoder_->CaptureInterrupt(TraceEvent::IRQMasked, Decoder_->ToIRQType(irq), cycleTime, state);
		CheckStopTrace(state);
	}

	void Debugger::TraceCaptureInterruptServicing(unsigned char irq, long cycleTime, CPUState state)
	{
		Decoder_->CaptureInterrupt(TraceEvent::IRQServicing, Decoder_->ToIRQType(irq), cycleTime, state);
		CheckStopTrace(state);
	}

	void Debugger::TraceCaptureInterruptExecuting(unsigned char irq, long cycleTime, CPUState state)
	{
		Decoder_->CaptureInterrupt(TraceEvent::IRQExecuting, Decoder_->ToIRQType(irq), cycleTime, state);
		CheckStopTrace(state);
	}

	void Debugger::TraceCaptureBefore(long cycles, CPUState state)
	{
		Decoder_->CaptureBefore(cycles, state);
		CheckStopTrace(state);
	}

	void Debugger::TraceCaptureAfter(long cycles, CPUState state)
	{
		Decoder_->CaptureAfter(cycles, state);
		CheckStopTrace(state);
	}

	void Debugger::CheckStopTrace(CPUState state)
	{
		if (!TraceRunning_)
		{
			return;
		}

		// Max samples reached?
		if (Decoder_->GetSampleCount() >= TraceMaxSamples_)
		{
			TraceRunning_ = false;
			TraceEnabled_ = false;
			TraceTriggerChanged_ = true;
			return;
		}

		// Hit a Stop Address?
		if (find(TraceStopTriggers_.begin(), TraceStopTriggers_.end(), state.PC) != TraceStopTriggers_.end())
		{
			TraceRunning_ = false;
			TraceEnabled_ = false;
			TraceTriggerChanged_ = true;
			return;
		}
	}

	void Debugger::TraceCaptureScreenEvent(TraceEvent evt, double cycles)
	{
		if (!TraceRunning_ || !TraceScreen_)
		{
			return;
		}

		Decoder_->CaptureScreenEvent(evt, cycles);
	}

	void Debugger::TraceEmulatorCycle(TraceEvent evt, int state, double lineNS, double irqNS, double soundNS, double cycles, double drift)
	{
		if (!TraceRunning_ || !TraceEmulation_)
		{
			return;
		}

		Decoder_->CaptureEmulatorCycle(evt, state, lineNS, irqNS, soundNS, cycles, drift);
	}


	void Debugger::LockTrace() const
	{
		Section_.Lock();
	}
	
	void Debugger::UnlockTrace() const
	{
		Section_.Unlock();
	}


	void Debugger::SetTraceEnable()
	{
		TraceEnabled_ = true;
	}

	void Debugger::SetTraceDisable()
	{
		TraceEnabled_ = false;
	}

	void Debugger::TraceStart()
	{
		TraceRunning_ = true;
	}

	void Debugger::TraceStop()
	{
		TraceRunning_ = false;
		TraceEnabled_ = false;
	}

	void Debugger::ResetTrace()
	{
		TraceEnabled_ = false;
		TraceRunning_ = false;
		{
			SectionLocker lock(Section_);
			Decoder_->Reset(TraceMaxSamples_);
		}
	}

	void Debugger::SetTraceMaxSamples(long samples)
	{
		SectionLocker lock(Section_);

		TraceMaxSamples_ = samples;
		Decoder_->Reset(TraceMaxSamples_);
	}

	void Debugger::SetTraceStartTriggers(triggerbuffer_type startTriggers)
	{
		SectionLocker lock(Section_);

		TraceStartTriggers_ = move(startTriggers);
		TraceTriggerChanged_ = true;
	}

	void Debugger::SetTraceStopTriggers(triggerbuffer_type stopTriggers)
	{
		SectionLocker lock(Section_);

		TraceStopTriggers_ = move(stopTriggers);
		TraceTriggerChanged_ = true;
	}

	void Debugger::SetTraceOptions(bool screen, bool emulation)
	{
		SectionLocker lock(Section_);

		TraceScreen_ = screen;
		TraceEmulation_ = emulation;
	}

	long Debugger::GetTraceSamples() const
	{
		SectionLocker lock(Section_);

		return Decoder_->GetSampleCount();
	}

	const Debugger::Debugger::tracebuffer_type& Debugger::GetTraceResult() const
	{
		return Decoder_->GetTrace();
	}

	void Debugger::SetTraceMark(int mark, long sample)
	{
		SectionLocker lock(Section_);

		TraceMarks_[mark] = sample;
	}


	void Debugger::ClearTraceMarks()
	{
		SectionLocker lock(Section_);

		TraceMarks_.clear();
	}

	void Debugger::GetTraceMarkSamples(tracebuffer_type &result) const
	{
		SectionLocker lock(Section_);

		result.clear();

		auto& trace = Decoder_->GetTrace();

		for (const auto& kv: TraceMarks_)
		{
			result.push_back(trace[kv.second]);
		}
	}

	void Debugger::QueueRun()
	{
		ApplyHaltpoints(true);
		SectionLocker lock(Section_);

		PendingCommand_ = ExecutionMode::Run;
		HasPendingCommand_ = true;
	}

	void Debugger::QueueStep()
	{
		SectionLocker lock(Section_);

		PendingCommand_ = ExecutionMode::Step;
		HasPendingCommand_ = true;
	}

	void Debugger::QueueHalt()
	{
		ApplyHaltpoints(false);
		SectionLocker lock(Section_);

		PendingCommand_ = ExecutionMode::Halt;
		HasPendingCommand_ = true;
	}

	void Debugger::ToggleRun() {
		if (IsHalted()) {
			PauseAudio(0);
			QueueRun();
		} else {
			QueueHalt();
			PauseAudio(1);
		}
		return;
	}

	void Debugger::Update()
	{
		{
			SectionLocker lock(Section_);

			ProcessorState_ = CPUGetState();

			if (BreakpointsChanged_)
			{
				CPUSetBreakpoints(Breakpoints_);
				BreakpointsChanged_ = false;
			}

			if (TraceTriggerChanged_)
			{
				CPUSetTraceTriggers(TraceStartTriggers_);
				TraceTriggerChanged_ = false;
			}

			if (HasPendingCommandNoLock())
			{
				ExecutionMode_ = ProcessNewModeNoLock(ConsumePendingCommandNoLock());
			}
			
			if (HasPendingWriteNoLock())
			{
				ProcessWrite(ConsumePendingWriteNoLock());
			}

		}

		for (const auto& callback : RegisteredClients_)
		{
			callback.second->OnUpdate();
		}

	}


	Debugger::ExecutionMode Debugger::ProcessNewModeNoLock(ExecutionMode cpuCmd) const
	{
		// Run -> Halt
		if (ExecutionMode_ == ExecutionMode::Run && cpuCmd == ExecutionMode::Halt)
		{
			return ExecutionMode::Halt;
		}
		// Halt -> Run
		if (ExecutionMode_ == ExecutionMode::Halt && cpuCmd == ExecutionMode::Run)
		{
			return ExecutionMode::Run;
		}
		// Halt -> Step
		if (ExecutionMode_ == ExecutionMode::Halt && cpuCmd == ExecutionMode::Step)
		{
			return ExecutionMode::Step;
		}

		return ExecutionMode_;
	}


	void Debugger::QueueWrite(unsigned short addr, unsigned char value)
	{
		SectionLocker lock(Section_);

		PendingWrite_ = PendingWrite(addr, value);
		HasPendingWrite_ = true;
	}


	bool Debugger::HasPendingWriteNoLock() const
	{
		return HasPendingWrite_;
	}

	Debugger::PendingWrite Debugger::ConsumePendingWriteNoLock()
	{
		if (!HasPendingWriteNoLock())
		{
			throw std::runtime_error("No pending memory writes to consume");
		}

		HasPendingWrite_ = false;
		return PendingWrite_;
	}

	void Debugger::ProcessWrite(PendingWrite memWrite)
	{
		MemWrite8(memWrite.value, memWrite.addr);
	}

	bool Debugger::Halt_Enabled()
	{
		return Halt_Enabled_TF;
	}

	void Debugger::Enable_Halt(bool flag)
	{
		Halt_Enabled_TF = flag;
	}

	bool Debugger::Break_Enabled()
	{
		return Break_Enabled_TF;
	}

	void Debugger::Enable_Break(bool flag)
	{
		Break_Enabled_TF = flag;
	}

} }


