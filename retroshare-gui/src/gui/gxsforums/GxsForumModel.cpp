#include <QApplication>
#include <QFontMetrics>
#include <QModelIndex>
#include <QIcon>

#include "util/qtthreadsutils.h"
#include "util/HandleRichText.h"
#include "util/DateTime.h"
#include "gui/gxs/GxsIdDetails.h"
#include "GxsForumModel.h"
#include "retroshare/rsgxsflags.h"
#include "retroshare/rsgxsforums.h"

//#define DEBUG_FORUMMODEL

Q_DECLARE_METATYPE(RsMsgMetaData);

std::ostream& operator<<(std::ostream& o, const QModelIndex& i);// defined elsewhere

RsGxsForumModel::RsGxsForumModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    initEmptyHierarchy(mPosts);


    mFilterColumn=0;
    mUseChildTS=false;
    mFlatView=false;
}

void RsGxsForumModel::initEmptyHierarchy(std::vector<ForumModelPostEntry>& posts)
{
    posts.resize(1);	// adds a sentinel item
    posts[0].mTitle = "Root sentinel post" ;
    posts[0].mParent = 0;
}

int RsGxsForumModel::rowCount(const QModelIndex& parent) const
{
    if(parent.column() > 0)
        return 0;

    if(mPosts.empty())	// security. Should never happen.
        return 0;

    if(!parent.isValid())
       return getChildrenCount(NULL);
    else
       return getChildrenCount(parent.internalPointer());
}

int RsGxsForumModel::columnCount(const QModelIndex &parent) const
{
	return COLUMN_THREAD_NB_COLUMNS ;
}

std::vector<std::pair<time_t,RsGxsMessageId> > RsGxsForumModel::getPostVersions(const RsGxsMessageId& mid) const
{
    auto it = mPostVersions.find(mid);

    if(it != mPostVersions.end())
        return it->second;
    else
        return std::vector<std::pair<time_t,RsGxsMessageId> >();
}

bool RsGxsForumModel::getPostData(const QModelIndex& i,ForumModelPostEntry& fmpe) const
{
	if(!i.isValid())
        return true;

    void *ref = i.internalPointer();
	uint32_t entry = 0;

	if(!convertRefPointerToTabEntry(ref,entry) || entry >= mPosts.size())
		return false ;

    fmpe = mPosts[entry];

	return true;

}

bool RsGxsForumModel::hasChildren(const QModelIndex &parent) const
{
    if(!parent.isValid())
        return true;

    void *ref = parent.internalPointer();
	uint32_t entry = 0;

	if(!convertRefPointerToTabEntry(ref,entry) || entry >= mPosts.size())
	{
#ifdef DEBUG_FORUMMODEL
		std::cerr << "hasChildren-2(" << parent << ") : " << false << std::endl;
#endif
		return false ;
	}

#ifdef DEBUG_FORUMMODEL
    std::cerr << "hasChildren-3(" << parent << ") : " << !mPosts[entry].mChildren.empty() << std::endl;
#endif
	return !mPosts[entry].mChildren.empty();
}

bool RsGxsForumModel::convertTabEntryToRefPointer(uint32_t entry,void *& ref)
{
	// the pointer is formed the following way:
	//
	//		[ 32 bits ]
	//
	// This means that the whole software has the following build-in limitation:
	//	  * 4 B   simultaenous posts. Should be enough !

	ref = reinterpret_cast<void*>( (intptr_t)entry );

	return true;
}

bool RsGxsForumModel::convertRefPointerToTabEntry(void *ref,uint32_t& entry)
{
    intptr_t val = (intptr_t)ref;

    if(val > (intptr_t)(~(uint32_t(0))))	// make sure the pointer is an int that fits in 32bits
    {
        std::cerr << "(EE) trying to make a ForumModelIndex out of a number that is larger than 2^32-1 !" << std::endl;
        return false ;
    }
	entry = uint32_t(val);

	return true;
}

QModelIndex RsGxsForumModel::index(int row, int column, const QModelIndex & parent) const
{
//    if(!hasIndex(row,column,parent))
    if(row < 0 || column < 0 || column >= COLUMN_THREAD_NB_COLUMNS)
		return QModelIndex();

    void *ref = getChildRef(parent.internalPointer(),row);
#ifdef DEBUG_FORUMMODEL
	std::cerr << "index-3(" << row << "," << column << " parent=" << parent << ") : " << createIndex(row,column,ref) << std::endl;
#endif
	return createIndex(row,column,ref) ;
}

QModelIndex RsGxsForumModel::parent(const QModelIndex& index) const
{
    if(!index.isValid())
        return QModelIndex();

    void *child_ref = index.internalPointer();
    int row=0;

    void *parent_ref = getParentRef(child_ref,row) ;

    if(parent_ref == NULL)		// root
        return QModelIndex() ;

    return createIndex(row,0,parent_ref);
}

Qt::ItemFlags RsGxsForumModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return 0;

    return QAbstractItemModel::flags(index);
}

void *RsGxsForumModel::getChildRef(void *ref,int row) const
{
    ForumModelIndex entry ;

    if(!convertRefPointerToTabEntry(ref,entry) || entry >= mPosts.size())
        return NULL ;

    void *new_ref;
    if(row >= mPosts[entry].mChildren.size())
        return NULL;

    convertTabEntryToRefPointer(mPosts[entry].mChildren[row],new_ref);

    return new_ref;
}

void *RsGxsForumModel::getParentRef(void *ref,int& row) const
{
    ForumModelIndex ref_entry;

    if(!convertRefPointerToTabEntry(ref,ref_entry) || ref_entry >= mPosts.size())
        return NULL ;

    ForumModelIndex parent_entry = mPosts[ref_entry].mParent;

    if(parent_entry == 0)		// top level index
    {
        row = 0;
        return NULL ;
    }
    else
    {
        void *parent_ref;
        convertTabEntryToRefPointer(parent_entry,parent_ref);
        row = mPosts[parent_entry].prow;

        return parent_ref;
    }
}

int RsGxsForumModel::getChildrenCount(void *ref) const
{
    uint32_t entry = 0 ;

    if(!convertRefPointerToTabEntry(ref,entry) || entry >= mPosts.size())
        return 0 ;

    return mPosts[entry].mChildren.size();
}

QVariant RsGxsForumModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if(role == Qt::DisplayRole)
		switch(section)
		{
		case COLUMN_THREAD_TITLE:        return tr("Title");
		case COLUMN_THREAD_DATE:         return tr("Date");
		case COLUMN_THREAD_AUTHOR:       return tr("Author");
		case COLUMN_THREAD_DISTRIBUTION: return tr("Distribution");
		default:
			return QVariant();
		}

	if(role == Qt::DecorationRole)
		switch(section)
		{
		case COLUMN_THREAD_DISTRIBUTION: return QIcon(":/icons/flag_green.png");
		case COLUMN_THREAD_READ:         return QIcon(":/images/message-state-read.png");
		default:
			return QVariant();
		}

	return QVariant();
}

QVariant RsGxsForumModel::data(const QModelIndex &index, int role) const
{
#ifdef DEBUG_FORUMMODEL
    std::cerr << "calling data(" << index << ") role=" << role << std::endl;
#endif

	if(!index.isValid())
		return QVariant();

	switch(role)
	{
	case Qt::SizeHintRole: return sizeHintRole(index.column()) ;
    case Qt::StatusTipRole:return QVariant();
    default: break;
	}

	void *ref = (index.isValid())?index.internalPointer():NULL ;
	uint32_t entry = 0;

#ifdef DEBUG_FORUMMODEL
	std::cerr << "data(" << index << ")" ;
#endif

	if(!ref)
	{
#ifdef DEBUG_FORUMMODEL
		std::cerr << " [empty]" << std::endl;
#endif
		return QVariant() ;
	}

	if(!convertRefPointerToTabEntry(ref,entry) || entry >= mPosts.size())
	{
#ifdef DEBUG_FORUMMODEL
		std::cerr << "Bad pointer: " << (void*)ref << std::endl;
#endif
		return QVariant() ;
	}

	const ForumModelPostEntry& fmpe(mPosts[entry]);

    if(role == Qt::FontRole)
    {
        QFont font ;
		font.setBold( (fmpe.mPostFlags & (ForumModelPostEntry::FLAG_POST_HAS_UNREAD_CHILDREN | ForumModelPostEntry::FLAG_POST_IS_PINNED)));
        return QVariant(font);
    }

    if(role == UnreadChildrenRole)
        return bool(fmpe.mPostFlags & ForumModelPostEntry::FLAG_POST_HAS_UNREAD_CHILDREN);

#ifdef DEBUG_FORUMMODEL
	std::cerr << " [ok]" << std::endl;
#endif

	switch(role)
	{
	case Qt::DisplayRole:    return displayRole   (fmpe,index.column()) ;
	case Qt::DecorationRole: return decorationRole(fmpe,index.column()) ;
	case Qt::ToolTipRole:	 return toolTipRole   (fmpe,index.column()) ;
	case Qt::UserRole:	 	 return userRole      (fmpe,index.column()) ;
	case Qt::TextColorRole:  return textColorRole (fmpe,index.column()) ;
	case Qt::BackgroundRole: return backgroundRole(fmpe,index.column()) ;

	case ThreadPinnedRole:   return pinnedRole    (fmpe,index.column()) ;
	case MissingRole:        return missingRole   (fmpe,index.column()) ;
	case StatusRole:         return statusRole    (fmpe,index.column()) ;
	default:
		return QVariant();
	}
}

QVariant RsGxsForumModel::textColorRole(const ForumModelPostEntry& fmpe,int column) const
{
    if( (fmpe.mPostFlags & ForumModelPostEntry::FLAG_POST_IS_MISSING))
        return QVariant(mTextColorMissing);

    if(IS_MSG_UNREAD(fmpe.mMsgStatus) || (fmpe.mPostFlags & ForumModelPostEntry::FLAG_POST_IS_PINNED))
        return QVariant(mTextColorUnread);
    else
        return QVariant(mTextColorRead);

	return QVariant();
}

QVariant RsGxsForumModel::statusRole(const ForumModelPostEntry& fmpe,int column) const
{
 	if(column != COLUMN_THREAD_DATA)
        return QVariant();

    return QVariant(fmpe.mMsgStatus);
}

QVariant RsGxsForumModel::missingRole(const ForumModelPostEntry& fmpe,int column) const
{
    if(column != COLUMN_THREAD_DATA)
        return QVariant();

    if(fmpe.mPostFlags & ForumModelPostEntry::FLAG_POST_IS_MISSING)
        return QVariant(true);
    else
        return QVariant(false);
}

QVariant RsGxsForumModel::toolTipRole(const ForumModelPostEntry& fmpe,int column) const
{
    if(column == COLUMN_THREAD_DISTRIBUTION)
		switch(fmpe.mReputationWarningLevel)
		{
		case 3: return QVariant(tr("Information for this identity is currently missing.")) ;
		case 2: return QVariant(tr("You have banned this ID. The message will not be\ndisplayed nor forwarded to your friends.")) ;
		case 1: return QVariant(tr("You have not set an opinion for this person,\n and your friends do not vote positively: Spam regulation \nprevents the message to be forwarded to your friends.")) ;
		case 0: return QVariant(tr("Message will be forwarded to your friends.")) ;
		default:
			return QVariant("[ERROR: missing reputation level information - contact the developers]");
		}

    if(column == COLUMN_THREAD_AUTHOR)
	{
		QString str,comment ;
		QList<QIcon> icons;

		if(!GxsIdDetails::MakeIdDesc(fmpe.mAuthorId, true, str, icons, comment,GxsIdDetails::ICON_TYPE_AVATAR))
			return QVariant();

		int S = QFontMetricsF(QApplication::font()).height();
		QImage pix( (*icons.begin()).pixmap(QSize(4*S,4*S)).toImage());

		QString embeddedImage;
		if(RsHtml::makeEmbeddedImage(pix.scaled(QSize(4*S,4*S), Qt::KeepAspectRatio, Qt::SmoothTransformation), embeddedImage, 8*S * 8*S))
			comment = "<table><tr><td>" + embeddedImage + "</td><td>" + comment + "</td></table>";

		return comment;
	}

    return QVariant();
}

QVariant RsGxsForumModel::pinnedRole(const ForumModelPostEntry& fmpe,int column) const
{
    if(column != COLUMN_THREAD_DATE)
        return QVariant();

    if(fmpe.mPostFlags & ForumModelPostEntry::FLAG_POST_IS_PINNED)
        return QVariant(true);
    else
        return QVariant(false);
}

QVariant RsGxsForumModel::backgroundRole(const ForumModelPostEntry& fmpe,int column) const
{
    if(fmpe.mPostFlags & ForumModelPostEntry::FLAG_POST_IS_PINNED)
        return QVariant(QBrush(QColor(255,200,180)));

    return QVariant();
}

QVariant RsGxsForumModel::sizeHintRole(int col) const
{
	float factor = QFontMetricsF(QApplication::font()).height()/14.0f ;

	switch(col)
	{
	default:
	case COLUMN_THREAD_TITLE:        return QVariant( QSize(factor * 170, factor*14 ));
	case COLUMN_THREAD_DATE:         return QVariant( QSize(factor * 75 , factor*14 ));
	case COLUMN_THREAD_AUTHOR:       return QVariant( QSize(factor * 75 , factor*14 ));
	case COLUMN_THREAD_DISTRIBUTION: return QVariant( QSize(factor * 15 , factor*14 ));
	}
}

QVariant RsGxsForumModel::authorRole(const ForumModelPostEntry& fmpe,int column) const
{
    if(column == COLUMN_THREAD_DATA)
        return QVariant(QString::fromStdString(fmpe.mAuthorId.toStdString()));

    return QVariant();
}

QVariant RsGxsForumModel::sortRole(const ForumModelPostEntry& fmpe,int column) const
{
    if(column == COLUMN_THREAD_DATA)
        return QVariant(QString::number(fmpe.mPublishTs)); // we should probably have leading zeroes here

    return QVariant();
}

QVariant RsGxsForumModel::displayRole(const ForumModelPostEntry& fmpe,int col) const
{
	switch(col)
	{
		case COLUMN_THREAD_TITLE:  if(fmpe.mPostFlags & ForumModelPostEntry::FLAG_POST_IS_REDACTED)
									return QVariant(tr("[ ... Redacted message ... ]"));
								else if(fmpe.mPostFlags & ForumModelPostEntry::FLAG_POST_IS_PINNED)
									return QVariant(tr("[PINNED] ") + QString::fromUtf8(fmpe.mTitle.c_str()));
								else
									return QVariant(QString::fromUtf8(fmpe.mTitle.c_str()));

		case COLUMN_THREAD_READ:return QVariant();
    	case COLUMN_THREAD_DATE:       {
    							    QDateTime qtime;
									qtime.setTime_t(fmpe.mPublishTs);

									return QVariant(DateTime::formatDateTime(qtime));
    							}

		case COLUMN_THREAD_DISTRIBUTION:
		case COLUMN_THREAD_AUTHOR: return QVariant();
		case COLUMN_THREAD_MSGID: return QVariant();
#ifdef TODO
	if (filterColumn == COLUMN_THREAD_CONTENT) {
		// need content for filter
		QTextDocument doc;
		doc.setHtml(QString::fromUtf8(msg.mMsg.c_str()));
		item->setText(COLUMN_THREAD_CONTENT, doc.toPlainText().replace(QString("\n"), QString(" ")));
	}
#endif
		default:
			return QVariant("[ TODO ]");
		}


	return QVariant("[ERROR]");
}

QVariant RsGxsForumModel::userRole(const ForumModelPostEntry& fmpe,int col) const
{
	switch(col)
    {
     	case COLUMN_THREAD_AUTHOR:   return QVariant(QString::fromStdString(fmpe.mAuthorId.toStdString()));
     	case COLUMN_THREAD_MSGID:    return QVariant(QString::fromStdString(fmpe.mMsgId.toStdString()));
    default:
        return QVariant();
    }
}

QVariant RsGxsForumModel::decorationRole(const ForumModelPostEntry& fmpe,int col) const
{
    if(col == COLUMN_THREAD_DISTRIBUTION)
        return QVariant(fmpe.mReputationWarningLevel);
    else if(col == COLUMN_THREAD_READ)
        return QVariant(fmpe.mMsgStatus);
    else
		return QVariant();
}

void RsGxsForumModel::setForum(const RsGxsGroupId& forum_group_id)
{
    //if(mForumGroup.mMeta.mGroupId == forum_group_id)
    //    return ;

    // we do not set mForumGroupId yet. We'll do it when the forum data is updated.

    update_posts(forum_group_id);
}

void RsGxsForumModel::setPosts(const RsGxsForumGroup& group, const std::vector<ForumModelPostEntry>& posts,const std::map<RsGxsMessageId,std::vector<std::pair<time_t,RsGxsMessageId> > >& post_versions)
{
    emit layoutAboutToBeChanged();

    mForumGroup = group;
    mPosts = posts;
    mPostVersions = post_versions;

    // now update prow for all posts

    for(uint32_t i=0;i<mPosts.size();++i)
        for(uint32_t j=0;j<mPosts[i].mChildren.size();++j)
            mPosts[mPosts[i].mChildren[j]].prow = j;

    mPosts[0].prow = 0;

    bool has_unread_below,has_read_below ;
    recursUpdateReadStatus(0,has_unread_below,has_read_below) ;
#ifndef DEBUG_FORUMMODEL
    debug_dump();
#endif

	emit layoutChanged();
	emit forumLoaded();
    emit dataChanged(createIndex(0,0,(void*)NULL), createIndex(0,COLUMN_THREAD_NB_COLUMNS-1,(void*)NULL));
}

void RsGxsForumModel::update_posts(const RsGxsGroupId& group_id)
{
	RsThread::async([this, group_id]()
	{
        // 1 - get message data from p3GxsForums

        std::list<RsGxsGroupId> forumIds;
		std::vector<RsGxsForumMsg> messages;
		std::vector<RsGxsForumGroup> groups;

        forumIds.push_back(group_id);

		if(!rsGxsForums->getForumsInfo(forumIds,groups))
		{
			std::cerr << __PRETTY_FUNCTION__ << " failed to retrieve forum group info for forum " << group_id << std::endl;
			return;
        }

		if(!rsGxsForums->getForumsContent(forumIds,messages))
		{
			std::cerr << __PRETTY_FUNCTION__ << " failed to retrieve forum message info for forum " << group_id << std::endl;
			return;
		}

        // 2 - sort the messages into a proper hierarchy

        auto post_versions = new std::map<RsGxsMessageId,std::vector<std::pair<time_t, RsGxsMessageId> > >() ;
        std::vector<ForumModelPostEntry> *vect = new std::vector<ForumModelPostEntry>();
        RsGxsForumGroup group = groups[0];

        computeMessagesHierarchy(group,messages,*vect,*post_versions);

        // 3 - update the model in the UI thread.

        RsQThreadUtils::postToObject( [group,vect,post_versions,this]()
		{
			/* Here it goes any code you want to be executed on the Qt Gui
			 * thread, for example to update the data model with new information
			 * after a blocking call to RetroShare API complete, note that
			 * Qt::QueuedConnection is important!
			 */

            setPosts(group,*vect,*post_versions) ;

            delete vect;
            delete post_versions;


		}, this );

    });
}

ForumModelIndex RsGxsForumModel::addEntry(std::vector<ForumModelPostEntry>& posts,const ForumModelPostEntry& entry,ForumModelIndex parent)
{
    uint32_t N = posts.size();
    posts.push_back(entry);

    posts[N].mParent = parent;
    posts[parent].mChildren.push_back(N);

    std::cerr << "Added new entry " << N << " children of " << parent << std::endl;

    if(N == parent)
        std::cerr << "(EE) trying to add a post as its own parent!" << std::endl;
    return ForumModelIndex(N);
}

void RsGxsForumModel::generateMissingItem(const RsGxsMessageId &msgId,ForumModelPostEntry& entry)
{
    entry.mPostFlags = ForumModelPostEntry::FLAG_POST_IS_MISSING ;
    entry.mTitle = std::string(tr("[ ... Missing Message ... ]").toUtf8());
    entry.mMsgId = msgId;
    entry.mAuthorId.clear();
    entry.mPublishTs=0;
    entry.mReputationWarningLevel = 3;
}

void RsGxsForumModel::convertMsgToPostEntry(const RsGxsForumGroup& mForumGroup,const RsGxsForumMsg& msg, bool useChildTS, uint32_t filterColumn,ForumModelPostEntry& fentry)
{
    fentry.mTitle     = msg.mMeta.mMsgName;
    fentry.mAuthorId  = msg.mMeta.mAuthorId;
    fentry.mMsgId     = msg.mMeta.mMsgId;
    fentry.mPublishTs = msg.mMeta.mPublishTs;
    fentry.mPostFlags = 0;
    fentry.mMsgStatus = msg.mMeta.mMsgStatus;

    if(mForumGroup.mPinnedPosts.ids.find(msg.mMeta.mMsgId) != mForumGroup.mPinnedPosts.ids.end())
		fentry.mPostFlags |= ForumModelPostEntry::FLAG_POST_IS_PINNED;

	// Early check for a message that should be hidden because its author
	// is flagged with a bad reputation

    uint32_t idflags =0;
	RsReputations::ReputationLevel reputation_level = rsReputations->overallReputationLevel(msg.mMeta.mAuthorId,&idflags) ;
	bool redacted = false;

    if(reputation_level == RsReputations::REPUTATION_LOCALLY_NEGATIVE)
        fentry.mPostFlags |= ForumModelPostEntry::FLAG_POST_IS_REDACTED;

    // We use a specific item model for forums in order to handle the post pinning.

    if(reputation_level == RsReputations::REPUTATION_UNKNOWN)
        fentry.mReputationWarningLevel = 3 ;
    else if(reputation_level == RsReputations::REPUTATION_LOCALLY_NEGATIVE)
        fentry.mReputationWarningLevel = 2 ;
    else if(reputation_level < rsGxsForums->minReputationForForwardingMessages(mForumGroup.mMeta.mSignFlags,idflags))
        fentry.mReputationWarningLevel = 1 ;
    else
        fentry.mReputationWarningLevel = 0 ;

#ifdef TODO
    // This is an attempt to put pinned posts on the top. We should rather use a QSortFilterProxyModel here.
	QString itemSort = QString::number(msg.mMeta.mPublishTs);//Don't need to format it as for sort.

	if (useChildTS)
	{
		for(QTreeWidgetItem *grandParent = parent; grandParent!=NULL; grandParent = grandParent->parent())
		{
			//Update Parent Child TimeStamp
			QString oldTSSort = grandParent->data(COLUMN_THREAD_DATE, ROLE_THREAD_SORT).toString();

			QString oldCTSSort = oldTSSort.split("|").at(0);
			QString oldPTSSort = oldTSSort.contains("|") ? oldTSSort.split(" | ").at(1) : oldCTSSort;
#ifdef SHOW_COMBINED_DATES
			QString oldTSText = grandParent->text(COLUMN_THREAD_DATE);
			QString oldCTSText = oldTSText.split("|").at(0);
			QString oldPTSText = oldTSText.contains("|") ? oldTSText.split(" | ").at(1) : oldCTSText;//If first time parent get only its mPublishTs
 #endif
			if (oldCTSSort.toDouble() < itemSort.toDouble())
			{
#ifdef SHOW_COMBINED_DATES
				grandParent->setText(COLUMN_THREAD_DATE, DateTime::formatDateTime(qtime) + " | " + oldPTSText);
#endif
				grandParent->setData(COLUMN_THREAD_DATE, ROLE_THREAD_SORT, itemSort + " | " + oldPTSSort);
			}
		}
	}
#endif
}

static bool decreasing_time_comp(const std::pair<time_t,RsGxsMessageId>& e1,const std::pair<time_t,RsGxsMessageId>& e2) { return e2.first < e1.first ; }

void RsGxsForumModel::computeMessagesHierarchy(const RsGxsForumGroup& forum_group,
                                               const std::vector<RsGxsForumMsg>& msgs_array,
                                               std::vector<ForumModelPostEntry>& posts,
                                               std::map<RsGxsMessageId,std::vector<std::pair<time_t,RsGxsMessageId> > >& mPostVersions
                                               )
{
    std::cerr << "updating messages data with " << msgs_array.size() << " messages" << std::endl;

//#ifdef DEBUG_FORUMS
    std::cerr << "Retrieved group data: " << std::endl;
    std::cerr << "  Group ID: " << forum_group.mMeta.mGroupId << std::endl;
    std::cerr << "  Admin lst: " << forum_group.mAdminList.ids.size() << " elements." << std::endl;
    for(auto it(forum_group.mAdminList.ids.begin());it!=forum_group.mAdminList.ids.end();++it)
        std::cerr << "    " << *it << std::endl;
    std::cerr << "  Pinned Post: " << forum_group.mPinnedPosts.ids.size() << " messages." << std::endl;
    for(auto it(forum_group.mPinnedPosts.ids.begin());it!=forum_group.mPinnedPosts.ids.end();++it)
        std::cerr << "    " << *it << std::endl;
//#endif

	/* get messages */
	std::map<RsGxsMessageId,RsGxsForumMsg> msgs;

	for(uint32_t i=0;i<msgs_array.size();++i)
	{
#ifdef DEBUG_FORUMS
		std::cerr << "Adding message " << msgs_array[i].mMeta.mMsgId << " with parent " << msgs_array[i].mMeta.mParentId << " to message map" << std::endl;
#endif
		msgs[msgs_array[i].mMeta.mMsgId] = msgs_array[i] ;
	}

	int count = msgs.size();
	int pos = 0;
//	int steps = count / PROGRESSBAR_MAX;
	int step = 0;

    initEmptyHierarchy(posts);

    // ThreadList contains the list of parent threads. The algorithm below iterates through all messages
    // and tries to establish parenthood relationships between them, given that we only know the
    // immediate parent of a message and now its children. Some messages have a missing parent and for them
    // a fake top level parent is generated.

    // In order to be efficient, we first create a structure that lists the children of every mesage ID in the list.
    // Then the hierarchy of message is build by attaching the kids to every message until all of them have been processed.
    // The messages with missing parents will be the last ones remaining in the list.

	std::list<std::pair< RsGxsMessageId, ForumModelIndex > > threadStack;
    std::map<RsGxsMessageId,std::list<RsGxsMessageId> > kids_array ;
    std::set<RsGxsMessageId> missing_parents;

    // First of all, remove all older versions of posts. This is done by first adding all posts into a hierarchy structure
    // and then removing all posts which have a new versions available. The older versions are kept appart.

#ifdef DEBUG_FORUMS
	std::cerr << "GxsForumsFillThread::run() Collecting post versions" << std::endl;
#endif
    mPostVersions.clear();
    std::list<RsGxsMessageId> msg_stack ;

	for ( std::map<RsGxsMessageId,RsGxsForumMsg>::iterator msgIt = msgs.begin(); msgIt != msgs.end();++msgIt)
    {
        if(!msgIt->second.mMeta.mOrigMsgId.isNull() && msgIt->second.mMeta.mOrigMsgId != msgIt->second.mMeta.mMsgId)
		{
#ifdef DEBUG_FORUMS
			std::cerr << "  Post " << msgIt->second.mMeta.mMsgId << " is a new version of " << msgIt->second.mMeta.mOrigMsgId << std::endl;
#endif
			std::map<RsGxsMessageId,RsGxsForumMsg>::iterator msgIt2 = msgs.find(msgIt->second.mMeta.mOrigMsgId);

			// Ensuring that the post exists allows to only collect the existing data.

			if(msgIt2 == msgs.end())
				continue ;

			// Make sure that the author is the same than the original message, or is a moderator. This should always happen when messages are constructed using
            // the UI but nothing can prevent a nasty user to craft a new version of a message with his own signature.

			if(msgIt2->second.mMeta.mAuthorId != msgIt->second.mMeta.mAuthorId)
			{
				if( !IS_FORUM_MSG_MODERATION(msgIt->second.mMeta.mMsgFlags) )			// if authors are different the moderation flag needs to be set on the editing msg
					continue ;

				if( forum_group.mAdminList.ids.find(msgIt->second.mMeta.mAuthorId)==forum_group.mAdminList.ids.end())	// if author is not a moderator, continue
					continue ;
			}

			// always add the post a self version

			if(mPostVersions[msgIt->second.mMeta.mOrigMsgId].empty())
				mPostVersions[msgIt->second.mMeta.mOrigMsgId].push_back(std::make_pair(msgIt2->second.mMeta.mPublishTs,msgIt2->second.mMeta.mMsgId)) ;

			mPostVersions[msgIt->second.mMeta.mOrigMsgId].push_back(std::make_pair(msgIt->second.mMeta.mPublishTs,msgIt->second.mMeta.mMsgId)) ;
		}
    }

    // The following code assembles all new versions of a given post into the same array, indexed by the oldest version of the post.

    for(auto it(mPostVersions.begin());it!=mPostVersions.end();++it)
    {
		auto& v(it->second) ;

        for(int32_t i=0;i<v.size();++i)
        {
            if(v[i].second != it->first)
			{
				RsGxsMessageId sub_msg_id = v[i].second ;

				auto it2 = mPostVersions.find(sub_msg_id);

				if(it2 != mPostVersions.end())
				{
					for(int32_t j=0;j<it2->second.size();++j)
						if(it2->second[j].second != sub_msg_id)	// dont copy it, since it is already present at slot i
							v.push_back(it2->second[j]) ;

					mPostVersions.erase(it2) ;	// it2 is never equal to it
				}
			}
        }
    }


    // Now remove from msg ids, all posts except the most recent one. And make the mPostVersion be indexed by the most recent version of the post,
    // which corresponds to the item in the tree widget.

#ifdef DEBUG_FORUMS
	std::cerr << "Final post versions: " << std::endl;
#endif
	std::map<RsGxsMessageId,std::vector<std::pair<time_t,RsGxsMessageId> > > mTmp;
    std::map<RsGxsMessageId,RsGxsMessageId> most_recent_versions ;

    for(auto it(mPostVersions.begin());it!=mPostVersions.end();++it)
    {
#ifdef DEBUG_FORUMS
        std::cerr << "Original post: " << it.key() << std::endl;
#endif
        // Finally, sort the posts from newer to older

        std::sort(it->second.begin(),it->second.end(),decreasing_time_comp) ;

#ifdef DEBUG_FORUMS
		std::cerr << "   most recent version " << (*it)[0].first << "  " << (*it)[0].second << std::endl;
#endif
        for(int32_t i=1;i<it->second.size();++i)
        {
			msgs.erase(it->second[i].second) ;

#ifdef DEBUG_FORUMS
            std::cerr << "   older version " << (*it)[i].first << "  " << (*it)[i].second << std::endl;
#endif
        }

        mTmp[it->second[0].second] = it->second ;	// index the versions map by the ID of the most recent post.

		// Now make sure that message parents are consistent. Indeed, an old post may have the old version of a post as parent. So we need to change that parent
		// to the newest version. So we create a map of which is the most recent version of each message, so that parent messages can be searched in it.

        for(int i=1;i<it->second.size();++i)
            most_recent_versions[it->second[i].second] = it->second[0].second ;
    }
    mPostVersions = mTmp ;

    // The next step is to find the top level thread messages. These are defined as the messages without
    // any parent message ID.

    // this trick is needed because while we remove messages, the parents a given msg may already have been removed
    // and wrongly understand as a missing parent.

	std::map<RsGxsMessageId,RsGxsForumMsg> kept_msgs;

	for ( std::map<RsGxsMessageId,RsGxsForumMsg>::iterator msgIt = msgs.begin(); msgIt != msgs.end();++msgIt)
    {

        if(mFlatView || msgIt->second.mMeta.mParentId.isNull())
		{

			/* add all threads */
			const RsGxsForumMsg& msg = msgIt->second;

#ifdef DEBUG_FORUMS
			std::cerr << "GxsForumsFillThread::run() Adding TopLevel Thread: mId: " << msg.mMeta.mMsgId << std::endl;
#endif

            ForumModelPostEntry entry;
			convertMsgToPostEntry(forum_group,msg, mUseChildTS, mFilterColumn,entry);

            ForumModelIndex entry_index = addEntry(posts,entry,0);

			if (!mFlatView)
				threadStack.push_back(std::make_pair(msg.mMeta.mMsgId,entry_index)) ;

			//calculateExpand(msg, item);
			//mItems.append(entry_index);
		}
		else
        {
#ifdef DEBUG_FORUMS
			std::cerr << "GxsForumsFillThread::run() Storing kid " << msgIt->first << " of message " << msgIt->second.mMeta.mParentId << std::endl;
#endif
            // The same missing parent may appear multiple times, so we first store them into a unique container.

            RsGxsMessageId parent_msg = msgIt->second.mMeta.mParentId;

            if(msgs.find(parent_msg) == msgs.end())
            {
                // also check that the message is not versionned

                std::map<RsGxsMessageId,RsGxsMessageId>::const_iterator mrit = most_recent_versions.find(parent_msg) ;

                if(mrit != most_recent_versions.end())
                    parent_msg = mrit->second ;
                else
					missing_parents.insert(parent_msg);
            }

            kids_array[parent_msg].push_back(msgIt->first) ;
            kept_msgs.insert(*msgIt) ;
        }
    }

    msgs = kept_msgs;

    // Also create a list of posts by time, when they are new versions of existing posts. Only the last one will have an item created.

    // Add a fake toplevel item for the parent IDs that we dont actually have.

    for(std::set<RsGxsMessageId>::const_iterator it(missing_parents.begin());it!=missing_parents.end();++it)
	{
		// add dummy parent item
        ForumModelPostEntry e ;
        generateMissingItem(*it,e);

        ForumModelIndex e_index = addEntry(posts,e,0);	// no parent -> parent is level 0
		//mItems.append( e_index );

		threadStack.push_back(std::make_pair(*it,e_index)) ;
	}
#ifdef DEBUG_FORUMS
	std::cerr << "GxsForumsFillThread::run() Processing stack:" << std::endl;
#endif
    // Now use a stack to go down the hierarchy

	while (!threadStack.empty())
    {
        std::pair<RsGxsMessageId, uint32_t> threadPair = threadStack.front();
		threadStack.pop_front();

        std::map<RsGxsMessageId, std::list<RsGxsMessageId> >::iterator it = kids_array.find(threadPair.first) ;

#ifdef DEBUG_FORUMS
		std::cerr << "GxsForumsFillThread::run() Node: " << threadPair.first << std::endl;
#endif
        if(it == kids_array.end())
            continue ;


        for(std::list<RsGxsMessageId>::const_iterator it2(it->second.begin());it2!=it->second.end();++it2)
        {
            // We iterate through the top level thread items, and look for which message has the current item as parent.
            // When found, the item is put in the thread list itself, as a potential new parent.

            std::map<RsGxsMessageId,RsGxsForumMsg>::iterator mit = msgs.find(*it2) ;

            if(mit == msgs.end())
			{
				std::cerr << "GxsForumsFillThread::run()    Cannot find submessage " << *it2 << " !!!" << std::endl;
				continue ;
			}

            const RsGxsForumMsg& msg(mit->second) ;
#ifdef DEBUG_FORUMS
			std::cerr << "GxsForumsFillThread::run()    adding sub_item " << msg.mMeta.mMsgId << std::endl;
#endif


            ForumModelPostEntry e ;
			convertMsgToPostEntry(forum_group,msg,mUseChildTS,mFilterColumn,e) ;
            ForumModelIndex e_index = addEntry(posts,e, threadPair.second);

			//calculateExpand(msg, item);

			/* add item to process list */
			threadStack.push_back(std::make_pair(msg.mMeta.mMsgId, e_index));

			msgs.erase(mit);
		}

#ifdef DEBUG_FORUMS
		std::cerr << "GxsForumsFillThread::run() Erasing entry " << it->first << " from kids tab." << std::endl;
#endif
        kids_array.erase(it) ; // This is not strictly needed, but it improves performance by reducing the search space.
	}

#ifdef DEBUG_FORUMS
    std::cerr << "Kids array now has " << kids_array.size() << " elements" << std::endl;
    for(std::map<RsGxsMessageId,std::list<RsGxsMessageId> >::const_iterator it(kids_array.begin());it!=kids_array.end();++it)
    {
        std::cerr << "Node " << it->first << std::endl;
        for(std::list<RsGxsMessageId>::const_iterator it2(it->second.begin());it2!=it->second.end();++it2)
            std::cerr << "  " << *it2 << std::endl;
    }

	std::cerr << "GxsForumsFillThread::run() stopped: " << (wasStopped() ? "yes" : "no") << std::endl;
#endif
}

void RsGxsForumModel::setMsgReadStatus(const QModelIndex& i,bool read_status,bool with_children)
{
	if(!i.isValid())
		return ;

	void *ref = i.internalPointer();
	uint32_t entry = 0;

	if(!convertRefPointerToTabEntry(ref,entry) || entry >= mPosts.size())
		return ;

    bool has_unread_below,has_read_below;
    recursSetMsgReadStatus(entry,read_status,with_children) ;
	recursUpdateReadStatus(0,has_unread_below,has_read_below);

    emit dataChanged(createIndex(0,0,(void*)NULL), createIndex(0,COLUMN_THREAD_NB_COLUMNS-1,(void*)NULL));
}

void RsGxsForumModel::recursSetMsgReadStatus(ForumModelIndex i,bool read_status,bool with_children)
{
    if(read_status)
		mPosts[i].mMsgStatus = 0;
    else
		mPosts[i].mMsgStatus = GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;

    uint32_t token;
	rsGxsForums->setMessageReadStatus(token,std::make_pair( mForumGroup.mMeta.mGroupId, mPosts[i].mMsgId ), read_status);

    if(!with_children)
        return;

    for(uint32_t j=0;j<mPosts[i].mChildren.size();++j)
        recursSetMsgReadStatus(mPosts[i].mChildren[j],read_status,with_children);
}

void RsGxsForumModel::recursUpdateReadStatus(ForumModelIndex i,bool& has_unread_below,bool& has_read_below)
{
    has_unread_below =  IS_MSG_UNREAD(mPosts[i].mMsgStatus);
    has_read_below   = !IS_MSG_UNREAD(mPosts[i].mMsgStatus);

    for(uint32_t j=0;j<mPosts[i].mChildren.size();++j)
    {
        bool ub,rb;

        recursUpdateReadStatus(mPosts[i].mChildren[j],ub,rb);

        has_unread_below = has_unread_below || ub ;
        has_read_below   = has_read_below   || rb ;

        if(ub && rb)		// optimization
            break;
    }

    if(has_unread_below)
		mPosts[i].mPostFlags |=  ForumModelPostEntry::FLAG_POST_HAS_UNREAD_CHILDREN;
    else
		mPosts[i].mPostFlags &= ~ForumModelPostEntry::FLAG_POST_HAS_UNREAD_CHILDREN;

    if(has_read_below)
		mPosts[i].mPostFlags |=  ForumModelPostEntry::FLAG_POST_HAS_READ_CHILDREN;
    else
		mPosts[i].mPostFlags &= ~ForumModelPostEntry::FLAG_POST_HAS_READ_CHILDREN;
}

QModelIndex RsGxsForumModel::getIndexOfMessage(const RsGxsMessageId& mid) const
{
    // brutal search. This is not so nice, so dont call that in a loop!

    for(uint32_t i=0;i<mPosts.size();++i)
        if(mPosts[i].mMsgId == mid)
        {
            void *ref ;
            convertTabEntryToRefPointer(i,ref);

            return createIndex(mPosts[i].prow,0,ref);
        }

    return QModelIndex();
}

#ifdef TO_REMVOVE
void RsGxsForumModel::test_iterator() const
{
    const_iterator it(*this);

    while(it)
    {
        std::cerr << "Current node: " << *it << std::endl;
		++it;
    }
}

QModelIndex RsGxsForumModel::getNextIndex(const QModelIndex& i,bool unread_only) const
{
    test_iterator();

    ForumModelIndex fmi ;
    convertRefPointerToTabEntry(i.internalPointer(),fmi);

    // Algorithm is simple: visit children recursively. When none available, go to parent. We need of course a stack of parents to the current index.

    const_iterator it(*this,fmi);

    do {++it;} while(bool(it) && (!unread_only || IS_MSG_UNREAD(mPosts[*it].mMsgStatus)));

    if(!it)
        return QModelIndex();

    void *ref ;
    convertTabEntryToRefPointer(*it,ref);

    return createIndex(mPosts[*it].prow,0,ref);
}


RsGxsForumModel::const_iterator::const_iterator(const RsGxsForumModel &Model, ForumModelIndex i)
	: model(Model)
{
    if(i >= model.mPosts.size())
    {
        std::cerr << "(EE) constructed a RsGxsForumModel::const_iterator from invalid index " << i << std::endl;
        kid = -1;
        return;
    }
    // create a stack or parents
    parent_stack.clear();

    if(i==0)
    {
        current_parent = 0;
        kid =0;
        return;
    }
    current_parent = model.mPosts[i].mParent;
    ForumModelIndex j(i);
    kid = model.mPosts[i].prow;

    while(j != 0)
    {
        parent_stack.push_front(model.mPosts[j].prow);
		j = model.mPosts[i].mParent;
    }
}

ForumModelIndex RsGxsForumModel::const_iterator::operator*() const
{
    if(current_parent >= model.mPosts.size() || kid < 0 || kid >= (int)model.mPosts[current_parent].mChildren.size())
    {
        std::cerr << "(EE) operator* on an invalid RsGxsForumModel::const_iterator"<< std::endl;
        return 0;
    }

    return model.mPosts[current_parent].mChildren[kid];
}

void RsGxsForumModel::const_iterator::operator++()
{
    kid++;
    while(kid >= (int)model.mPosts[current_parent].mChildren.size())
    {
        current_parent = model.mPosts[current_parent].mParent;
        kid = parent_stack.back()+1;

        parent_stack.pop_back();

    }

	if(current_parent == 0 && kid >= (int)model.mPosts[current_parent].mChildren.size())
	{
		kid = -1;
		return;
	}

    while(!model.mPosts[model.mPosts[current_parent].mChildren[kid]].mChildren.empty())
    {
        parent_stack.push_back(kid);
        current_parent = model.mPosts[current_parent].mChildren[kid];
        kid = 0;
    }
}

RsGxsForumModel::const_iterator::operator bool() const
{
    return kid >= 0;
}
#endif

static void recursPrintModel(const std::vector<ForumModelPostEntry>& entries,ForumModelIndex index,int depth)
{
    const ForumModelPostEntry& e(entries[index]);

	QDateTime qtime;
	qtime.setTime_t(e.mPublishTs);

    std::cerr << std::string(depth*2,' ') << index << " : " << e.mAuthorId.toStdString() << " "
              << QString("%1").arg((uint32_t)e.mPostFlags,8,16,QChar('0')).toStdString() << " "
              << QString("%1").arg((uint32_t)e.mMsgStatus,8,16,QChar('0')).toStdString() << " "
              << qtime.toString().toStdString() << " \"" << e.mTitle << "\"" << std::endl;

    for(uint32_t i=0;i<e.mChildren.size();++i)
        recursPrintModel(entries,e.mChildren[i],depth+1);
}


void RsGxsForumModel::debug_dump()
{
    std::cerr << "Model data dump:" << std::endl;
    std::cerr << "  Entries: " << mPosts.size() << std::endl;

    // non recursive print

    for(uint32_t i=0;i<mPosts.size();++i)
    {
		const ForumModelPostEntry& e(mPosts[i]);

		std::cerr << "    " << i << " : " << e.mMsgId << " (from " << e.mAuthorId.toStdString() << ") "
                  << QString("%1").arg((uint32_t)e.mPostFlags,8,16,QChar('0')).toStdString() << " "
                  << QString("%1").arg((uint32_t)e.mMsgStatus,8,16,QChar('0')).toStdString() << " ";

    	for(uint32_t i=0;i<e.mChildren.size();++i)
            std::cerr << " " << e.mChildren[i] ;

		QDateTime qtime;
		qtime.setTime_t(e.mPublishTs);

        std::cerr << " (" << e.mParent << ")";
		std::cerr << " " << qtime.toString().toStdString() << " \"" << e.mTitle << "\"" << std::endl;
    }

    // recursive print
    recursPrintModel(mPosts,ForumModelIndex(0),0);
}



