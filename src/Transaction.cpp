#include <cassert>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>

#include "Application.h"
#include "Transaction.h"
#include "Wallet.h"
#include "BitcoinUtil.h"
#include "Serializer.h"
#include "SerializedTransaction.h"
#include "Log.h"

Transaction::Transaction(const SerializedTransaction::pointer& sit, bool bValidate)
	: mInLedger(0), mStatus(INVALID), mTransaction(sit)
{
	try
	{
		mFromPubKey.setAccountPublic(mTransaction->getSigningPubKey());
		mTransactionID	= mTransaction->getTransactionID();
		mAccountFrom	= mTransaction->getSourceAccount();
	}
	catch(...)
	{
		return;
	}

	if (!bValidate || checkSign())
		mStatus = NEW;
}

Transaction::pointer Transaction::sharedTransaction(const std::vector<unsigned char>&vucTransaction, bool bValidate)
{
	try
	{
		Serializer			s(vucTransaction);
		SerializerIterator	sit(s);

		SerializedTransaction::pointer	st	= boost::make_shared<SerializedTransaction>(boost::ref(sit));

		return boost::make_shared<Transaction>(st, bValidate);
	}
	catch (...)
	{
		Log(lsWARNING) << "Exception constructing transaction";
		return boost::shared_ptr<Transaction>();
	}
}

//
// Generic transaction construction
//

Transaction::Transaction(
	TransactionType ttKind,
	const NewcoinAddress&	naPublicKey,
	const NewcoinAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag) :
	mStatus(NEW)
{
	mAccountFrom	= naSourceAccount;
	mFromPubKey		= naPublicKey;
	assert(mFromPubKey.isValid());

	mTransaction	= boost::make_shared<SerializedTransaction>(ttKind);

	Log(lsINFO) << str(boost::format("Transaction: account: %s") % naSourceAccount.humanAccountID());
	Log(lsINFO) << str(boost::format("Transaction: mAccountFrom: %s") % mAccountFrom.humanAccountID());

	mTransaction->setSigningPubKey(mFromPubKey);
	mTransaction->setSourceAccount(mAccountFrom);
	mTransaction->setSequence(uSeq);
	mTransaction->setTransactionFee(saFee);

	if (uSourceTag)
	{
		mTransaction->makeITFieldPresent(sfSourceTag);
		mTransaction->setITFieldU32(sfSourceTag, uSourceTag);
	}
}

bool Transaction::sign(const NewcoinAddress& naAccountPrivate)
{
	bool	bResult	= true;

	if (!naAccountPrivate.isValid())
	{
		Log(lsWARNING) << "No private key for signing";
		bResult	= false;
	}
	getSTransaction()->sign(naAccountPrivate);

	if (bResult)
	{
		updateID();
	}
	else
	{
		mStatus = INCOMPLETE;
	}

	return bResult;
}

//
// AccountSet
//

Transaction::pointer Transaction::setAccountSet(
	const NewcoinAddress& naPrivateKey,
	bool								bEmailHash,
	const uint128&						uEmailHash,
	bool								bWalletLocator,
	const uint256&						uWalletLocator,
	const NewcoinAddress&				naMessagePublic,
	bool								bDomain,
	const std::vector<unsigned char>&	vucDomain,
	bool								bTransferRate,
	const uint32						uTransferRate,
	bool								bPublish,
	const uint256&						uPublishHash,
	const uint32						uPublishSize
	)
{
	if (!bEmailHash)
		mTransaction->setITFieldH128(sfEmailHash, uEmailHash);

	if (!bWalletLocator)
		mTransaction->setITFieldH256(sfWalletLocator, uWalletLocator);

	if (naMessagePublic.isValid())
		mTransaction->setITFieldVL(sfMessageKey, naMessagePublic.getAccountPublic());

	if (bDomain)
		mTransaction->setITFieldVL(sfDomain, vucDomain);

	if (bTransferRate)
		mTransaction->setITFieldU32(sfTransferRate, uTransferRate);

	if (bPublish)
	{
		mTransaction->setITFieldH256(sfPublishHash, uPublishHash);
		mTransaction->setITFieldU32(sfPublishSize, uPublishSize);
	}

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedAccountSet(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress& naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	bool								bEmailHash,
	const uint128&						uEmailHash,
	bool								bWalletLocator,
	const uint256&						uWalletLocator,
	const NewcoinAddress&				naMessagePublic,
	bool								bDomain,
	const std::vector<unsigned char>&	vucDomain,
	bool								bTransferRate,
	const uint32						uTransferRate,
	bool								bPublish,
	const uint256&						uPublishHash,
	const uint32						uPublishSize)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttACCOUNT_SET, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setAccountSet(naPrivateKey, bEmailHash, uEmailHash, bWalletLocator, uWalletLocator,
		naMessagePublic,
		bDomain, vucDomain, bTransferRate, uTransferRate, bPublish, uPublishHash, uPublishSize);
}

//
// Claim
//

Transaction::pointer Transaction::setClaim(
	const NewcoinAddress& naPrivateKey,
	const std::vector<unsigned char>& vucGenerator,
	const std::vector<unsigned char>& vucPubKey,
	const std::vector<unsigned char>& vucSignature)
{
	mTransaction->setITFieldVL(sfGenerator, vucGenerator);
	mTransaction->setITFieldVL(sfPublicKey, vucPubKey);
	mTransaction->setITFieldVL(sfSignature, vucSignature);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedClaim(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	uint32 uSourceTag,
	const std::vector<unsigned char>& vucGenerator,
	const std::vector<unsigned char>& vucPubKey,
	const std::vector<unsigned char>& vucSignature)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttCLAIM,
						naPublicKey, naPublicKey,
						0,		// Sequence of 0.
						0,		// Free.
						uSourceTag);

	return tResult->setClaim(naPrivateKey, vucGenerator, vucPubKey, vucSignature);
}

//
// Create
//

Transaction::pointer Transaction::setCreate(
	const NewcoinAddress&	naPrivateKey,
	const NewcoinAddress&	naCreateAccountID,
	const STAmount&			saFund)
{
	mTransaction->setITFieldU32(sfFlags, tfCreateAccount);
	mTransaction->setITFieldAccount(sfDestination, naCreateAccountID);
	mTransaction->setITFieldAmount(sfAmount, saFund);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedCreate(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress& naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const NewcoinAddress&	naCreateAccountID,
	const STAmount&			saFund)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttPAYMENT, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setCreate(naPrivateKey, naCreateAccountID, saFund);
}

//
// CreditSet
//

Transaction::pointer Transaction::setCreditSet(
	const NewcoinAddress&	naPrivateKey,
	const STAmount&			saLimitAmount,
	bool					bQualityIn,
	uint32					uQualityIn,
	bool					bQualityOut,
	uint32					uQualityOut)
{
	mTransaction->setITFieldAmount(sfLimitAmount, saLimitAmount);

	if (bQualityIn)
		mTransaction->setITFieldU32(sfQualityIn, uQualityIn);

	if (bQualityOut)
		mTransaction->setITFieldU32(sfQualityOut, uQualityOut);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedCreditSet(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const STAmount&			saLimitAmount,
	bool					bQualityIn,
	uint32					uQualityIn,
	bool					bQualityOut,
	uint32					uQualityOut)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttCREDIT_SET, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setCreditSet(naPrivateKey,
		saLimitAmount,
		bQualityIn, uQualityIn,
		bQualityOut, uQualityOut);
}

//
// NicknameSet
//

Transaction::pointer Transaction::setNicknameSet(
	const NewcoinAddress&				naPrivateKey,
	const uint256&						uNickname,
	bool								bSetOffer,
	const STAmount&						saMinimumOffer,
	const std::vector<unsigned char>&	vucSignature)
{
	mTransaction->setITFieldH256(sfNickname, uNickname);

	// XXX Make sure field is present even for 0!
	if (bSetOffer)
		mTransaction->setITFieldAmount(sfMinimumOffer, saMinimumOffer);

	if (!vucSignature.empty())
		mTransaction->setITFieldVL(sfSignature, vucSignature);

	sign(naPrivateKey);

	return shared_from_this();
}

// --> bSetOffer: true, change offer
// --> saMinimumOffer: 0 to remove.
Transaction::pointer Transaction::sharedNicknameSet(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&				naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	const uint256&						uNickname,
	bool								bSetOffer,
	const STAmount&						saMinimumOffer,
	const std::vector<unsigned char>&	vucSignature)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttNICKNAME_SET, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setNicknameSet(naPrivateKey, uNickname, bSetOffer, saMinimumOffer, vucSignature);
}

//
// OfferCreate
//

Transaction::pointer Transaction::setOfferCreate(
	const NewcoinAddress&				naPrivateKey,
	bool								bPassive,
	const STAmount&						saTakerPays,
	const STAmount&						saTakerGets,
	uint32								uExpiration)
{
	if (bPassive)
		mTransaction->setITFieldU32(sfFlags, tfPassive);

	mTransaction->setITFieldAmount(sfTakerPays, saTakerPays);
	mTransaction->setITFieldAmount(sfTakerGets, saTakerGets);

	if (uExpiration)
		mTransaction->setITFieldU32(sfExpiration, uExpiration);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedOfferCreate(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&				naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	bool								bPassive,
	const STAmount&						saTakerPays,
	const STAmount&						saTakerGets,
	uint32								uExpiration)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttOFFER_CREATE, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setOfferCreate(naPrivateKey, bPassive, saTakerPays, saTakerGets, uExpiration);
}

//
// OfferCancel
//

Transaction::pointer Transaction::setOfferCancel(
	const NewcoinAddress&				naPrivateKey,
	uint32								uSequence)
{
	mTransaction->setITFieldU32(sfOfferSequence, uSequence);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedOfferCancel(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&				naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	uint32								uSequence)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttOFFER_CANCEL, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setOfferCancel(naPrivateKey, uSequence);
}

//
// PasswordFund
//

Transaction::pointer Transaction::setPasswordFund(
	const NewcoinAddress&	naPrivateKey,
	const NewcoinAddress&	naDstAccountID)
{
	mTransaction->setITFieldAccount(sfDestination, naDstAccountID);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedPasswordFund(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const NewcoinAddress&	naDstAccountID)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttPASSWORD_FUND, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setPasswordFund(naPrivateKey, naDstAccountID);
}

//
// PasswordSet
//

Transaction::pointer Transaction::setPasswordSet(
	const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&				naAuthKeyID,
	const std::vector<unsigned char>&	vucGenerator,
	const std::vector<unsigned char>&	vucPubKey,
	const std::vector<unsigned char>&	vucSignature)
{
	mTransaction->setITFieldAccount(sfAuthorizedKey, naAuthKeyID);
	mTransaction->setITFieldVL(sfGenerator, vucGenerator);
	mTransaction->setITFieldVL(sfPublicKey, vucPubKey);
	mTransaction->setITFieldVL(sfSignature, vucSignature);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedPasswordSet(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	uint32								uSourceTag,
	const NewcoinAddress&				naAuthKeyID,
	const std::vector<unsigned char>&	vucGenerator,
	const std::vector<unsigned char>&	vucPubKey,
	const std::vector<unsigned char>&	vucSignature)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttPASSWORD_SET,
						naPublicKey, naPublicKey,
						0,		// Sequence of 0.
						0,		// Free.
						uSourceTag);

	return tResult->setPasswordSet(naPrivateKey, naAuthKeyID, vucGenerator, vucPubKey, vucSignature);
}

//
// Payment
//

Transaction::pointer Transaction::setPayment(
	const NewcoinAddress&	naPrivateKey,
	const NewcoinAddress&	naDstAccountID,
	const STAmount&			saAmount,
	const STAmount&			saSendMax,
	const STPathSet&		spsPaths,
	const bool				bPartial,
	const bool				bLimit)
{
	mTransaction->setITFieldAccount(sfDestination, naDstAccountID);
	mTransaction->setITFieldAmount(sfAmount, saAmount);

	if (saAmount != saSendMax || saAmount.getCurrency() != saSendMax.getCurrency())
	{
		mTransaction->setITFieldAmount(sfSendMax, saSendMax);
	}

	if (spsPaths.getPathCount())
	{
		mTransaction->setITFieldPathSet(sfPaths, spsPaths);
	}

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedPayment(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&	naSourceAccount,
	uint32					uSeq,
	const STAmount&			saFee,
	uint32					uSourceTag,
	const NewcoinAddress&	naDstAccountID,
	const STAmount&			saAmount,
	const STAmount&			saSendMax,
	const STPathSet&		spsPaths,
	const bool				bPartial,
	const bool				bLimit)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttPAYMENT, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setPayment(naPrivateKey, naDstAccountID, saAmount, saSendMax, spsPaths, bPartial, bLimit);
}

//
// WalletAdd
//

Transaction::pointer Transaction::setWalletAdd(
	const NewcoinAddress&				naPrivateKey,
	const STAmount&						saAmount,
	const NewcoinAddress&				naAuthKeyID,
	const NewcoinAddress&				naNewPubKey,
	const std::vector<unsigned char>&	vucSignature)
{
	mTransaction->setITFieldAmount(sfAmount, saAmount);
	mTransaction->setITFieldAccount(sfAuthorizedKey, naAuthKeyID);
	mTransaction->setITFieldVL(sfPublicKey, naNewPubKey.getAccountPublic());
	mTransaction->setITFieldVL(sfSignature, vucSignature);

	sign(naPrivateKey);

	return shared_from_this();
}

Transaction::pointer Transaction::sharedWalletAdd(
	const NewcoinAddress& naPublicKey, const NewcoinAddress& naPrivateKey,
	const NewcoinAddress&				naSourceAccount,
	uint32								uSeq,
	const STAmount&						saFee,
	uint32								uSourceTag,
	const STAmount&						saAmount,
	const NewcoinAddress&				naAuthKeyID,
	const NewcoinAddress&				naNewPubKey,
	const std::vector<unsigned char>&	vucSignature)
{
	pointer	tResult	= boost::make_shared<Transaction>(ttWALLET_ADD, naPublicKey, naSourceAccount, uSeq, saFee, uSourceTag);

	return tResult->setWalletAdd(naPrivateKey, saAmount, naAuthKeyID, naNewPubKey, vucSignature);
}

//
// Misc.
//

bool Transaction::checkSign() const
{
	assert(mFromPubKey.isValid());
	return mTransaction->checkSign(mFromPubKey);
}

void Transaction::setStatus(TransStatus ts, uint32 lseq)
{
	mStatus		= ts;
	mInLedger	= lseq;
}

void Transaction::saveTransaction(const Transaction::pointer& txn)
{
	txn->save();
}

bool Transaction::save()
{
	if ((mStatus == INVALID) || (mStatus == REMOVED)) return false;

	char status;
	switch (mStatus)
	{
	 case NEW:			status = TXN_SQL_NEW;		break;
	 case INCLUDED:		status = TXN_SQL_INCLUDED;	break;
	 case CONFLICTED:	status = TXN_SQL_CONFLICT;	break;
	 case COMMITTED:	status = TXN_SQL_VALIDATED;	break;
	 case HELD:			status = TXN_SQL_HELD;		break;
	 default:			status = TXN_SQL_UNKNOWN;
	}

	std::string exists = boost::str(boost::format("SELECT Status FROM Transactions WHERE TransID = '%s';")
		% mTransaction->getTransactionID().GetHex());

	Database *db = theApp->getTxnDB()->getDB();
	ScopedLock dbLock = theApp->getTxnDB()->getDBLock();
	if (SQL_EXISTS(db, exists)) return false;
	return
		db->executeSQL(mTransaction->getSQLInsertHeader() + mTransaction->getSQL(getLedger(), status) + ";");
}

Transaction::pointer Transaction::transactionFromSQL(const std::string& sql)
{
	Serializer rawTxn;
	std::string status;
	uint32 inLedger;

	int txSize = 2048;
	rawTxn.resize(txSize);

	{
		ScopedLock sl(theApp->getTxnDB()->getDBLock());
		Database* db = theApp->getTxnDB()->getDB();

		if (!db->executeSQL(sql, true) || !db->startIterRows())
			return Transaction::pointer();

		db->getStr("Status", status);
		inLedger = db->getInt("LedgerSeq");
		txSize = db->getBinary("RawTxn", &*rawTxn.begin(), rawTxn.getLength());
		if (txSize > rawTxn.getLength())
		{
			rawTxn.resize(txSize);
			db->getBinary("RawTxn", &*rawTxn.begin(), rawTxn.getLength());
		}
		db->endIterRows();
	}
	rawTxn.resize(txSize);

	SerializerIterator it(rawTxn);
	SerializedTransaction::pointer txn = boost::make_shared<SerializedTransaction>(boost::ref(it));
	Transaction::pointer tr = boost::make_shared<Transaction>(txn, true);

	TransStatus st(INVALID);
	switch (status[0])
	{
		case TXN_SQL_NEW:			st = NEW;			break;
		case TXN_SQL_CONFLICT:		st = CONFLICTED;	break;
		case TXN_SQL_HELD:			st = HELD;			break;
		case TXN_SQL_VALIDATED:		st = COMMITTED;		break;
		case TXN_SQL_INCLUDED:		st = INCLUDED;		break;
		case TXN_SQL_UNKNOWN:							break;
		default: assert(false);
	}
	tr->setStatus(st);
	tr->setLedger(inLedger);
	return tr;
}

Transaction::pointer Transaction::load(const uint256& id)
{
	std::string sql = "SELECT LedgerSeq,Status,RawTxn FROM Transactions WHERE TransID='";
	sql.append(id.GetHex());
	sql.append("';");
	return transactionFromSQL(sql);
}

Transaction::pointer Transaction::findFrom(const NewcoinAddress& fromID, uint32 seq)
{
	std::string sql = "SELECT LedgerSeq,Status,RawTxn FROM Transactions WHERE FromID='";
	sql.append(fromID.humanAccountID());
	sql.append("' AND FromSeq='");
	sql.append(boost::lexical_cast<std::string>(seq));
	sql.append("';");
	return transactionFromSQL(sql);
}

bool Transaction::convertToTransactions(uint32 firstLedgerSeq, uint32 secondLedgerSeq,
	bool checkFirstTransactions, bool checkSecondTransactions, const SHAMap::SHAMapDiff& inMap,
	std::map<uint256, std::pair<Transaction::pointer, Transaction::pointer> >& outMap)
{ // convert a straight SHAMap payload difference to a transaction difference table
  // return value: true=ledgers are valid, false=a ledger is invalid
	std::map<uint256, std::pair<SHAMapItem::pointer, SHAMapItem::pointer> >::const_iterator it;
	for(it = inMap.begin(); it != inMap.end(); ++it)
	{
		const uint256& id = it->first;
		const SHAMapItem::pointer& first = it->second.first;
		const SHAMapItem::pointer& second = it->second.second;

		Transaction::pointer firstTrans, secondTrans;
		if (!!first)
		{ // transaction in our table
			firstTrans = sharedTransaction(first->getData(), checkFirstTransactions);
			if ((firstTrans->getStatus() == INVALID) || (firstTrans->getID() != id ))
			{
				firstTrans->setStatus(INVALID, firstLedgerSeq);
				return false;
			}
			else firstTrans->setStatus(INCLUDED, firstLedgerSeq);
		}

		if (!!second)
		{ // transaction in other table
			secondTrans = sharedTransaction(second->getData(), checkSecondTransactions);
			if ((secondTrans->getStatus() == INVALID) || (secondTrans->getID() != id))
			{
				secondTrans->setStatus(INVALID, secondLedgerSeq);
				return false;
			}
			else secondTrans->setStatus(INCLUDED, secondLedgerSeq);
		}
		assert(firstTrans || secondTrans);
		if(firstTrans && secondTrans && (firstTrans->getStatus() != INVALID) && (secondTrans->getStatus() != INVALID))
			return false; // one or the other SHAMap is structurally invalid or a miracle has happened

		outMap[id] = std::pair<Transaction::pointer, Transaction::pointer>(firstTrans, secondTrans);
	}
	return true;
}

Json::Value Transaction::getJson(int options) const
{
	Json::Value ret(mTransaction->getJson(0));

	if (mInLedger) ret["inLedger"]=mInLedger;

	switch(mStatus)
	{
		case NEW: ret["status"] = "new"; break;
		case INVALID: ret["status"] = "invalid"; break;
		case INCLUDED: ret["status"] = "included"; break;
		case CONFLICTED: ret["status"] = "conflicted"; break;
		case COMMITTED: ret["status"] = "committed"; break;
		case HELD: ret["status"] = "held"; break;
		case REMOVED: ret["status"] = "removed"; break;
		case OBSOLETE: ret["status"] = "obsolete"; break;
		case INCOMPLETE: ret["status"] = "incomplete"; break;
		default: ret["status"] = "unknown";
	}

	return ret;
}

//
// Obsolete
//

static bool isHex(char j)
{
	if ((j >= '0') && (j <= '9')) return true;
	if ((j >= 'A') && (j <= 'F')) return true;
	if ((j >= 'a') && (j <= 'f')) return true;
	return false;
}

bool Transaction::isHexTxID(const std::string& txid)
{
	if (txid.size() != 64) return false;
	for (int i = 0; i < 64; ++i)
		if (!isHex(txid[i])) return false;
	return true;
}

// vim:ts=4
