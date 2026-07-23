from typing import List
from .. art import HR_SLIM, CONTRACT, BOT
from dataclasses import dataclass

@dataclass
class EmailTemplate(object):
    subject: str
    bodies: List[str]
    image: bytes = None


NEW_CLIENT = EmailTemplate("Welcome aboard", [
"""Welcome aboard,
No handouts, no safety nets. Oxygen costs money, and so do mistakes.

Your first task:
* Secure a shipment.
* Stabilize power.
* Check company alerts - we don't send emails for fun.
* Prove you're worth the investment.

- Central Operations Command
"""], HR_SLIM)

NEW_CONTRACT = EmailTemplate("New Contract", [
"""Hello,

A new contract has been assigned to you. We expect results.

Contract: {contract_name}
Objectives: 
{objectives}
Payment: {payment}""", """No delays, no excuses. 
If you're too slow, someone else will take the job.

Confirm receipt and execute immediately.

- Central Operations Command"""], CONTRACT)

WEEKLY_REPORT = EmailTemplate("W{week} Report", [
"""Review of operational performance for week {week} is complete. 
Quarter High: {quarter_high} credits.
Revenue: {total_revenue} credits
Expenses: {total_expenses} credits
Net Profit: {net_profit} credits""",
"""Bank Statement
Account balance: {credit_balance} credits
Outstanding debt: {debt_remaining} credits
Weekly debt rate: {debt_weekly_rate_pct}%
Expected weekly payment: {debt_weekly_payment} credits
Debt ceiling: {debt_ceiling} credits
Available borrowing: {debt_borrowing_capacity} credits
"""])

DEBT_BORROWED = EmailTemplate("Loan Disbursement", [
"""A loan disbursement has been credited to your account.

Amount borrowed: {amount} credits
Current balance: {credits} credits
Outstanding debt: {remaining} credits

- Meridian Financial Services
"""])

DEBT_REPAID = EmailTemplate("Loan Payment Received", [
"""Your loan payment has been received.

Amount repaid: {amount} credits
Current balance: {credits} credits
Outstanding debt: {remaining} credits

- Meridian Financial Services
"""])

FREE_BOT = EmailTemplate("Free Bot", [
"""You are now eligible for one complimentary Bot (c) unit to automate repetitive field work.
When nearby to it, select [E] to remote control or program functions.
- Central Operations Command
"""], BOT)
