#include "SubtitleTrackDataAsset.h"

int32 USubtitleTrackDataAsset::FindCueIndex(float TimeSeconds) const
{
	int32 BestIndex = INDEX_NONE;
	float BestStart = -1.f;

	for (int32 Index = 0; Index < Cues.Num(); ++Index)
	{
		const FXRU1SubtitleCue& Cue = Cues[Index];
		if (Cue.Text.IsEmpty() || TimeSeconds < Cue.StartTime)
		{
			continue;
		}

		// Конец реплики: своя длительность либо ближайший следующий вход.
		float EndTime = TNumericLimits<float>::Max();
		if (Cue.Duration > 0.f)
		{
			EndTime = Cue.StartTime + Cue.Duration;
		}
		else
		{
			for (const FXRU1SubtitleCue& Other : Cues)
			{
				if (!Other.Text.IsEmpty() && Other.StartTime > Cue.StartTime)
				{
					EndTime = FMath::Min(EndTime, Other.StartTime);
				}
			}
		}

		if (TimeSeconds >= EndTime)
		{
			continue;
		}

		// Наложение реплик — авторская ошибка, но показать надо одну и ту же при
		// каждом кадре: побеждает начавшаяся ПОЗЖЕ.
		if (Cue.StartTime > BestStart)
		{
			BestStart = Cue.StartTime;
			BestIndex = Index;
		}
	}

	return BestIndex;
}

FXRU1SubtitleLine USubtitleTrackDataAsset::MakeLine(int32 CueIndex, FName SourceId) const
{
	FXRU1SubtitleLine Line;
	if (Cues.IsValidIndex(CueIndex))
	{
		Line.Speaker = Cues[CueIndex].Speaker;
		Line.Text = Cues[CueIndex].Text;
		Line.SourceId = SourceId;
	}
	return Line;
}
