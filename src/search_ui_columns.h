#pragma once

namespace search {

namespace report_columns {
constexpr int SampleNo = 0;
constexpr int Name = 1;
constexpr int Barcode = 2;
constexpr int ReportTime = 3;
constexpr int Sex = 4;
constexpr int Age = 5;
constexpr int Bed = 6;
constexpr int PatientType = 7;
constexpr int Requester = 8;
constexpr int Reviewer = 9;
constexpr int GroupName = 10;
constexpr int ReviewStatus = 11;
constexpr int ConfirmStatus = 12;
constexpr int PrintStatus = 13;
constexpr int SelfServicePrintStatus = 14;
}  // namespace report_columns

namespace result_columns {
constexpr int ItemName = 0;
constexpr int Result = 1;
constexpr int LowerBound = 2;
constexpr int UpperBound = 3;
constexpr int Unit = 4;
constexpr int EnglishName = 5;
}  // namespace result_columns

}  // namespace search
