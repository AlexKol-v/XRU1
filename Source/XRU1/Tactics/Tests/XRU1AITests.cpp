// Регрессионные тесты AI (план 08_AI.md §4/AI-7).
//
// ЧТО ЗДЕСЬ ПРОВЕРЯЕТСЯ И ПОЧЕМУ ИМЕННО ЭТО.
//
// Тактический AI невозможно закрыть тестами целиком: выбор позиции упирается в
// навмеш и трейсы, исполнение — в GAS и анимации, а проверять это без мира
// нечем. Зато ПРАВИЛА, по которым считается ценность позиции, — чистая
// арифметика, и именно они ломались молча: каждый разбор лога начинался с
// вопроса «а точно ли отступление перестало любить открытые точки».
//
// Поэтому тесты закрывают ровно тот слой, который для этого и выделялся:
// `AUnitAIController::ScorePositionFacts` — факты плюс веса, без мира. Всё, что
// требует карты, остаётся в ручной матрице §6.
//
// Запуск: Session Frontend → Automation → фильтр `XRU1.AI`, либо из консоли
// `Automation RunTests XRU1.AI`.

#include "Misc/AutomationTest.h"
#include "UnitAIController.h"
#include "AIBehaviorProfileDataAsset.h"
#include "AIDecisionTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace XRU1AITests
{
	/** Веса по умолчанию — те же, что в C++ дефолтах профиля. */
	static FAIPositionScoringTuning DefaultTuning()
	{
		return FAIPositionScoringTuning();
	}

	/** Точка против одной угрозы: полностью укрыт, цель видна, ничего лишнего. */
	static FAIPositionFacts GoodCoverFacts(const FAIPositionScoringTuning& T)
	{
		FAIPositionFacts Facts;
		Facts.ThreatsScored = 1;
		Facts.CoverFactorSum = T.FullCoverFactor;
		Facts.ThreatsCovered = 1;
		Facts.ThreatsVisible = 1;
		Facts.ThreatDistance = 900.f;
		Facts.TravelDistance = 400.f;
		return Facts;
	}

	/** Та же точка, но открытая. */
	static FAIPositionFacts OpenFacts(const FAIPositionScoringTuning& T)
	{
		FAIPositionFacts Facts = GoodCoverFacts(T);
		Facts.CoverFactorSum = T.OpenCoverFactor;
		Facts.ThreatsCovered = 0;
		Facts.ThreatsExposed = 1;
		return Facts;
	}

	static constexpr float IdealRange = 900.f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FXRU1AICoverBeatsOpenTest,
	"XRU1.AI.Position.CoverBeatsOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FXRU1AICoverBeatsOpenTest::RunTest(const FString&)
{
	// Это базовый инвариант всего боевого AI: если он сломается, враг перестанет
	// прятаться, а по логу это будет выглядеть как «странно выбирает точки».
	const FAIPositionScoringTuning T = XRU1AITests::DefaultTuning();
	const float Covered = AUnitAIController::ScorePositionFacts(
		XRU1AITests::GoodCoverFacts(T), T, XRU1AITests::IdealRange, false, false);
	const float Open = AUnitAIController::ScorePositionFacts(
		XRU1AITests::OpenFacts(T), T, XRU1AITests::IdealRange, false, false);

	TestTrue(TEXT("укрытая точка должна быть лучше открытой"), Covered > Open);

	// Разрыв обязан быть КРУПНЫМ (XCOM: −4 против +1.1 × CoverDefenseWeight),
	// иначе его перебьёт любой другой член формулы.
	TestTrue(TEXT("разрыв укрытие/открытость должен перебивать бонус за линию огня"),
		(Covered - Open) > T.LineOfFireBonus);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FXRU1AIRetreatBreaksLineOfSightTest,
	"XRU1.AI.Position.RetreatBreaksLineOfSight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FXRU1AIRetreatBreaksLineOfSightTest::RunTest(const FString&)
{
	// Регрессия «прячется где попало»: при отступлении
	// точка, откуда врага ВИДНО, выигрывала у настоящего укрытия. Тест валится
	// ровно на возврате того знака.
	const FAIPositionScoringTuning T = XRU1AITests::DefaultTuning();

	FAIPositionFacts Hidden = XRU1AITests::GoodCoverFacts(T);
	Hidden.ThreatsVisible = 0;         // линия огня разорвана
	Hidden.ThreatDistance = 1400.f;    // и мы дальше от угрозы

	FAIPositionFacts StillExposedToFire = XRU1AITests::GoodCoverFacts(T);
	StillExposedToFire.ThreatsVisible = 1;
	StillExposedToFire.ThreatDistance = 1400.f;

	const float HiddenScore = AUnitAIController::ScorePositionFacts(
		Hidden, T, XRU1AITests::IdealRange, /*bRetreat=*/true, false);
	const float VisibleScore = AUnitAIController::ScorePositionFacts(
		StillExposedToFire, T, XRU1AITests::IdealRange, /*bRetreat=*/true, false);

	TestTrue(TEXT("при отступлении точка без линии огня должна выигрывать"),
		HiddenScore > VisibleScore);

	// А в обычном бою — наоборот: укрытие, из которого нельзя ответить, это угол.
	const float HiddenInCombat = AUnitAIController::ScorePositionFacts(
		Hidden, T, XRU1AITests::IdealRange, false, false);
	const float VisibleInCombat = AUnitAIController::ScorePositionFacts(
		StillExposedToFire, T, XRU1AITests::IdealRange, false, false);
	TestTrue(TEXT("в бою точка С линией огня должна выигрывать"),
		VisibleInCombat > HiddenInCombat);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FXRU1AIRouteRiskTest,
	"XRU1.AI.Position.RouteRiskPenalised",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FXRU1AIRouteRiskTest::RunTest(const FString&)
{
	// Пробежать под чужим овервотчем должно быть хуже, чем не пробежать,
	// но дешевле, чем ВСТАТЬ под ним.
	const FAIPositionScoringTuning T = XRU1AITests::DefaultTuning();
	const FAIPositionFacts Safe = XRU1AITests::GoodCoverFacts(T);

	FAIPositionFacts RiskyRoute = Safe;
	RiskyRoute.RouteOverwatchExposures = 1;

	FAIPositionFacts RiskyDestination = Safe;
	RiskyDestination.ThreatsOverwatching = 1;

	const float SafeScore = AUnitAIController::ScorePositionFacts(
		Safe, T, XRU1AITests::IdealRange, false, false);
	const float RouteScore = AUnitAIController::ScorePositionFacts(
		RiskyRoute, T, XRU1AITests::IdealRange, false, false);
	const float DestScore = AUnitAIController::ScorePositionFacts(
		RiskyDestination, T, XRU1AITests::IdealRange, false, false);

	TestTrue(TEXT("риск по маршруту должен штрафоваться"), RouteScore < SafeScore);
	TestTrue(TEXT("встать под овервотчем хуже, чем пробежать мимо"), DestScore < RouteScore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FXRU1AIAntiPendulumTest,
	"XRU1.AI.Position.AntiPendulum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FXRU1AIAntiPendulumTest::RunTest(const FString&)
{
	// Регрессия «убежал за укрытие и вернулся» (§3.12.1).
	const FAIPositionScoringTuning T = XRU1AITests::DefaultTuning();
	const FAIPositionFacts Fresh = XRU1AITests::GoodCoverFacts(T);

	FAIPositionFacts Revisited = Fresh;
	Revisited.bRecentlyOccupied = true;

	const float FreshScore = AUnitAIController::ScorePositionFacts(
		Fresh, T, XRU1AITests::IdealRange, false, false);
	const float RevisitedScore = AUnitAIController::ScorePositionFacts(
		Revisited, T, XRU1AITests::IdealRange, false, false);

	TestTrue(TEXT("точка, занятая в прошлые ходы, должна быть хуже свежей"),
		RevisitedScore < FreshScore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FXRU1AICrowdingTest,
	"XRU1.AI.Position.CrowdingDampensPositiveOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FXRU1AICrowdingTest::RunTest(const FString&)
{
	// Правило XCOM: множитель кучности применяется ТОЛЬКО к положительному скору.
	// К отрицательному он работал бы наоборот — делал плохую точку лучше.
	const FAIPositionScoringTuning T = XRU1AITests::DefaultTuning();

	FAIPositionFacts GoodCrowded = XRU1AITests::GoodCoverFacts(T);
	GoodCrowded.bCrowded = true;
	const float Good = AUnitAIController::ScorePositionFacts(
		XRU1AITests::GoodCoverFacts(T), T, XRU1AITests::IdealRange, false, false);
	const float Crowded = AUnitAIController::ScorePositionFacts(
		GoodCrowded, T, XRU1AITests::IdealRange, false, false);
	TestTrue(TEXT("хорошая точка в куче должна терять ценность"), Crowded < Good);

	FAIPositionFacts BadOpen = XRU1AITests::OpenFacts(T);
	FAIPositionFacts BadOpenCrowded = BadOpen;
	BadOpenCrowded.bCrowded = true;
	const float Bad = AUnitAIController::ScorePositionFacts(
		BadOpen, T, XRU1AITests::IdealRange, false, false);
	const float BadCrowded = AUnitAIController::ScorePositionFacts(
		BadOpenCrowded, T, XRU1AITests::IdealRange, false, false);
	TestTrue(TEXT("плохая точка не должна становиться лучше от кучности"),
		BadCrowded <= Bad);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FXRU1AIIdealRangeTest,
	"XRU1.AI.Position.IdealRangeAttracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FXRU1AIIdealRangeTest::RunTest(const FString&)
{
	// Именно этот член удерживает бота от «сбежаться вплотную» — без него
	// укрытие тянет прятаться, линия огня тянет видеть цель, а дистанцию не
	// оценивает никто (см. IdealRangeWeight).
	const FAIPositionScoringTuning T = XRU1AITests::DefaultTuning();

	FAIPositionFacts AtIdeal = XRU1AITests::GoodCoverFacts(T);
	AtIdeal.ThreatDistance = XRU1AITests::IdealRange;

	FAIPositionFacts TooClose = XRU1AITests::GoodCoverFacts(T);
	TooClose.ThreatDistance = 150.f;

	FAIPositionFacts TooFar = XRU1AITests::GoodCoverFacts(T);
	TooFar.ThreatDistance = XRU1AITests::IdealRange + T.IdealRangeFalloff * 1.5f;

	const float Ideal = AUnitAIController::ScorePositionFacts(
		AtIdeal, T, XRU1AITests::IdealRange, false, false);
	const float Close = AUnitAIController::ScorePositionFacts(
		TooClose, T, XRU1AITests::IdealRange, false, false);
	const float Far = AUnitAIController::ScorePositionFacts(
		TooFar, T, XRU1AITests::IdealRange, false, false);

	TestTrue(TEXT("идеальная дистанция лучше слишком близкой"), Ideal > Close);
	TestTrue(TEXT("идеальная дистанция лучше слишком дальней"), Ideal > Far);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FXRU1AIAdvanceIgnoresLostLineOfFireTest,
	"XRU1.AI.Position.AdvanceIgnoresLostLineOfFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FXRU1AIAdvanceIgnoresLostLineOfFireTest::RunTest(const FString&)
{
	// Иначе бот отвергает промежуточное укрытие без выстрела и наступает
	// открытой пробежкой — ровно то, от чего вводился режим bAdvance.
	const FAIPositionScoringTuning T = XRU1AITests::DefaultTuning();
	FAIPositionFacts NoLineOfFire = XRU1AITests::GoodCoverFacts(T);
	NoLineOfFire.ThreatsVisible = 0;

	const float Normal = AUnitAIController::ScorePositionFacts(
		NoLineOfFire, T, XRU1AITests::IdealRange, false, /*bAdvance=*/false);
	const float Advancing = AUnitAIController::ScorePositionFacts(
		NoLineOfFire, T, XRU1AITests::IdealRange, false, /*bAdvance=*/true);

	TestTrue(TEXT("при наступлении потеря линии огня штрафуется слабее"),
		Advancing > Normal);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
