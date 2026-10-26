#pragma once

#include "pyscript/script_math.hpp"
#include <memory>
#include <vector>

BW_BEGIN_NAMESPACE
namespace StockAudioDetail { struct Source; struct Voice; }
// The configured application owner exists for authored disabled settings too.
bool stockAudioApplicationAvailable();
// Opaque views of the same owners used by _WWISE PySound/PySoundObject.
typedef std::shared_ptr<StockAudioDetail::Source> StockAudioSource;
typedef std::shared_ptr<StockAudioDetail::Voice> StockAudioVoice;
StockAudioSource createStockAudioSource(const BW::string& name, bool auxiliary = false);
StockAudioVoice createStockAudioVoice(const BW::string& event, const BW::string& name);
StockAudioVoice createStockAudioAttachedVoice(const BW::string& event,
    MatrixProviderPtr provider, const Vector3& offset);
bool stockAudioVoiceValid(const StockAudioVoice& voice);
StockAudioSource stockAudioVoiceSource(const StockAudioVoice& voice);
bool playStockAudioVoice(StockAudioVoice& voice);
void stopStockAudioVoice(const StockAudioVoice& voice);
void stopStockAudioSource(const StockAudioSource& source);
bool playStockAudioSource(const StockAudioSource& source, const BW::string& event);
bool setStockAudioSourceMatrix(const StockAudioSource& source, MatrixProviderPtr provider);
// Static-scene audio uses front=(1,0,0), top=(0,1,0), MultiDirections.
bool setStockAudioSourcePositions(const StockAudioSource& source, const std::vector<Vector3>& positions);
bool setStockAudioSourceOffset(const StockAudioSource& source, const Vector3& offset);
bool setStockAudioSourceRTPC(const StockAudioSource& source, const BW::string& name, float value);
bool setStockAudioSourceSwitch(const StockAudioSource& source, const BW::string& group,
    const BW::string& value);
bool setStockAudioSourceVolume(const StockAudioSource& source, float volume);
bool setStockAudioGlobalRTPC(const BW::string& name, float value);
bool setStockAudioGlobalState(const BW::string& group, const BW::string& value);
PyObject* wrapStockAudioSource(const StockAudioSource& source);
BW_END_NAMESPACE
