#pragma once

#include "TransformerVoicing.h"
#include "TubeVoicing.h"
#include "GainTubeVoicing.h"
#include "VoicingState.h"

using TransformerVoicingState = VoicingState<TransformerVoicing::Schema>;
using TubeVoicingState = VoicingState<TubeVoicing::Schema>;
using GainTubeVoicingState = VoicingState<GainTubeVoicing::Schema>;
